/*
 * Offline SynthVoice renderer: preset or note sequence -> WAV file.
 */

#include "host_wav_writer.h"
#include "note_sequence.h"
#include "synth_presets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kBlockFrames = 512;

void PrintUsage(const char *prog) {
  std::fprintf(stderr,
               "Usage: %s --preset NAME [options]\n"
               "       %s --list\n"
               "\n"
               "Options:\n"
               "  --preset NAME       Preset to render (required unless --list)\n"
               "  --note N            MIDI note for single-note mode (default: preset)\n"
               "  --seconds S         Render length in seconds (default: 3 or sequence+tail)\n"
               "                      Rendering stops early once release tail finishes\n"
               "  --sequence SPEC     e.g. 60@0,64@0.5,67@1.0,release@2.0\n"
               "                      Prefix legato:NOTE@t for legato slides\n"
               "  -o PATH             Output WAV (default: /tmp/synth_<preset>.wav)\n"
               "  --play              Play with afplay after render (macOS)\n"
               "  --list              List presets\n",
               prog, prog);
}

void ListPresets() {
  size_t count = 0;
  const SynthPreset *presets = GetSynthPresets(&count);
  for (size_t i = 0; i < count; ++i) {
    const char *seq = presets[i].sequence_len > 0 ? " [sequence]" : "";
    std::printf("%-14s %s (default note %u)%s\n", presets[i].name,
                presets[i].description, presets[i].default_note, seq);
  }
}

bool PlayWav(const char *path) {
#if defined(__APPLE__)
  std::string cmd = std::string("afplay \"") + path + "\"";
  std::fprintf(stderr, "Playing: %s\n", path);
  return std::system(cmd.c_str()) == 0;
#else
  (void)path;
  std::fprintf(stderr, "Note: --play is only supported on macOS (afplay).\n");
  return false;
#endif
}

struct Options {
  const char *preset = nullptr;
  const char *output = nullptr;
  const char *sequence = nullptr;
  float seconds = 0.f;
  int note = -1;
  bool play = false;
  bool list = false;
};

bool ParseArgs(int argc, char **argv, Options &opt) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--list") == 0) {
      opt.list = true;
    } else if (std::strcmp(arg, "--play") == 0) {
      opt.play = true;
    } else if (std::strcmp(arg, "--preset") == 0 && i + 1 < argc) {
      opt.preset = argv[++i];
    } else if (std::strcmp(arg, "--note") == 0 && i + 1 < argc) {
      opt.note = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--seconds") == 0 && i + 1 < argc) {
      opt.seconds = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(arg, "--sequence") == 0 && i + 1 < argc) {
      opt.sequence = argv[++i];
    } else if (std::strcmp(arg, "-o") == 0 && i + 1 < argc) {
      opt.output = argv[++i];
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      return false;
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", arg);
      return false;
    }
  }
  return true;
}

bool RenderToWav(const SynthPreset &preset, const Options &opt,
                 const char *output_path) {
  std::vector<NoteEvent> cli_events;
  std::string parse_error;
  if (opt.sequence &&
      !ParseSequenceSpec(opt.sequence, cli_events, parse_error)) {
    std::fprintf(stderr, "Sequence parse error: %s\n", parse_error.c_str());
    return false;
  }

  const NoteEvent *events = preset.sequence;
  size_t event_count = preset.sequence_len;
  if (!cli_events.empty()) {
    events = cli_events.data();
    event_count = cli_events.size();
  }

  const bool sequence_mode = events && event_count > 0;
  float duration = opt.seconds;
  if (duration <= 0.f) {
    duration = sequence_mode ? DefaultSequenceDuration(events, event_count)
                             : 3.f;
  }

  SynthVoice::InitTables();
  SynthVoice voice;
  voice.Reset();

  NoteSequencePlayer player;
  if (sequence_mode) {
    player.Reset(&voice, &preset.params, events, event_count);
    player.DispatchUpTo(0.f);
  } else {
    const int note =
        opt.note >= 0 ? opt.note : static_cast<int>(preset.default_note);
    if (note < 0 || note > 127) {
      std::fprintf(stderr, "Invalid note: %d\n", note);
      return false;
    }
    TriggerNote(voice, preset.params, static_cast<uint8_t>(note), true);
  }

  HostWavWriter writer;
  if (!writer.Open(output_path)) {
    std::fprintf(stderr, "Failed to open output: %s\n", output_path);
    return false;
  }

  std::vector<fixed> buffer(static_cast<size_t>(kBlockFrames) * 2);
  const int total_frames =
      static_cast<int>(duration * HostWavWriter::kSampleRate);
  bool released = false;
  bool saw_active = false;
  int silent_frames = 0;
  constexpr int kTailFrames = 4410; // 0.1s padding after voice goes silent
  const float auto_release_at =
      sequence_mode ? 0.f : ImplicitReleaseTime(duration, preset.release_at);

  for (int rendered = 0; rendered < total_frames;) {
    const int frames =
        std::min(kBlockFrames, total_frames - rendered);
    const float block_end =
        static_cast<float>(rendered + frames) / HostWavWriter::kSampleRate;

    if (sequence_mode) {
      player.DispatchUpTo(block_end);
    } else if (!released && auto_release_at > 0.f &&
               block_end >= auto_release_at) {
      voice.Release();
      released = true;
    }

    voice.RenderBlock(buffer.data(), frames);
    writer.AddFixedBuffer(buffer.data(), frames);
    rendered += frames;

    if (voice.IsActive()) {
      saw_active = true;
      silent_frames = 0;
    } else if (saw_active) {
      silent_frames += frames;
      if (silent_frames >= kTailFrames) {
        break;
      }
    }
  }

  if (!writer.Finalize()) {
    std::fprintf(stderr, "Failed to finalize WAV: %s\n", output_path);
    return false;
  }

  std::fprintf(stderr, "Wrote %u frames (%.2fs) to %s\n", writer.sample_count(),
               writer.sample_count() / float(HostWavWriter::kSampleRate),
               output_path);
  return true;
}

} // namespace

int main(int argc, char **argv) {
  Options opt;
  if (!ParseArgs(argc, argv, opt)) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (opt.list) {
    ListPresets();
    return 0;
  }

  if (!opt.preset) {
    std::fprintf(stderr, "Missing --preset NAME\n");
    PrintUsage(argv[0]);
    return 1;
  }

  const SynthPreset *preset = FindSynthPreset(opt.preset);
  if (!preset) {
    std::fprintf(stderr, "Unknown preset: %s\n", opt.preset);
    ListPresets();
    return 1;
  }

  std::string output_path;
  if (opt.output) {
    output_path = opt.output;
  } else {
    output_path = std::string("/tmp/synth_") + opt.preset + ".wav";
  }

  if (!RenderToWav(*preset, opt, output_path.c_str())) {
    return 1;
  }

  if (opt.play) {
    if (!PlayWav(output_path.c_str())) {
      std::fprintf(stderr, "Rendered OK; play manually: %s\n",
                   output_path.c_str());
    }
  }

  return 0;
}
