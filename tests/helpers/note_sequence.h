/*
 * Note event scheduling for synth_render (single SynthVoice).
 */
#pragma once

#include "Application/Instruments/SynthVoice.h"
#include "synth_presets.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

inline void TriggerNote(SynthVoice &voice, const VoiceParams &params,
                        uint8_t note, bool retrigger) {
  voice.Trigger(params, SynthVoice::NoteToInc(note), note, 0x7F, retrigger);
}

class NoteSequencePlayer {
public:
  void Reset(SynthVoice *voice, const VoiceParams *params,
             const NoteEvent *events, size_t count) {
    voice_ = voice;
    params_ = params;
    events_ = events;
    count_ = count;
    next_ = 0;
  }

  void DispatchUpTo(float seconds) {
    if (!voice_ || !params_ || !events_) {
      return;
    }
    while (next_ < count_ && events_[next_].at_seconds <= seconds) {
      const NoteEvent &event = events_[next_];
      if (event.gate_off) {
        voice_->Release();
      } else {
        TriggerNote(*voice_, *params_, event.note, event.retrigger);
      }
      ++next_;
    }
  }

  bool exhausted() const { return next_ >= count_; }

private:
  SynthVoice *voice_ = nullptr;
  const VoiceParams *params_ = nullptr;
  const NoteEvent *events_ = nullptr;
  size_t count_ = 0;
  size_t next_ = 0;
};

inline float DefaultSequenceDuration(const NoteEvent *events, size_t count,
                                     float tail_seconds = 1.0f) {
  if (!events || count == 0) {
    return 3.f;
  }
  return std::max(events[count - 1].at_seconds + tail_seconds, 0.5f);
}

inline float ImplicitReleaseTime(float duration, float preset_release_at) {
  if (preset_release_at > 0.f) {
    return preset_release_at;
  }
  // Leave ~40% of the clip for release tail when preset does not specify one.
  return std::max(duration * 0.6f, duration - 2.f);
}

inline bool ParseSequenceSpec(const char *spec, std::vector<NoteEvent> &out,
                              std::string &error) {
  out.clear();
  error.clear();
  if (!spec || spec[0] == '\0') {
    error = "empty sequence";
    return false;
  }

  std::string input(spec);
  size_t start = 0;
  while (start < input.size()) {
    size_t comma = input.find(',', start);
    std::string token = input.substr(start, comma == std::string::npos
                                                ? std::string::npos
                                                : comma - start);
    start = (comma == std::string::npos) ? input.size() : comma + 1;

    while (!token.empty() && token.front() == ' ') {
      token.erase(token.begin());
    }
    while (!token.empty() && token.back() == ' ') {
      token.pop_back();
    }
    if (token.empty()) {
      continue;
    }

    bool legato = false;
    if (token.rfind("legato:", 0) == 0) {
      legato = true;
      token = token.substr(7);
    }

    size_t at = token.find('@');
    if (at == std::string::npos) {
      error = "expected NOTE@seconds or release@seconds, got: " + token;
      return false;
    }

    std::string left = token.substr(0, at);
    std::string right = token.substr(at + 1);
    char *end = nullptr;
    float when = std::strtof(right.c_str(), &end);
    if (end == right.c_str()) {
      error = "invalid time in: " + token;
      return false;
    }

    NoteEvent event{};
    event.at_seconds = when;
    event.retrigger = !legato;

    if (left == "release" || left == "off") {
      event.gate_off = true;
      out.push_back(event);
      continue;
    }

    long note = std::strtol(left.c_str(), &end, 10);
    if (end == left.c_str() || note < 0 || note > 127) {
      error = "invalid MIDI note in: " + token;
      return false;
    }
    event.note = static_cast<uint8_t>(note);
    out.push_back(event);
  }

  if (out.empty()) {
    error = "no events parsed";
    return false;
  }

  std::sort(out.begin(), out.end(),
            [](const NoteEvent &a, const NoteEvent &b) {
              return a.at_seconds < b.at_seconds;
            });
  return true;
}
