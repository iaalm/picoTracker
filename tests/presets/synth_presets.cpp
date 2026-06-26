#include "synth_presets.h"

#include <cstring>

VoiceParams DefaultVoiceParams() {
  VoiceParams p{};
  p.algorithm = 4;
  p.op1_wave = SYNTH_WAVE_SINE;
  p.op2_wave = SYNTH_WAVE_SINE;
  p.op3_wave = SYNTH_WAVE_SINE;
  p.op1_level = 0xFF;
  p.op1_ad = SynthAd(0, 0);
  p.op1_sr = SynthSr(0xF, 8);
  p.op2_ad = SynthAd(0, 0);
  p.op2_sr = SynthSr(0xF, 8);
  p.op3_ad = SynthAd(0, 0);
  p.op3_sr = SynthSr(0xF, 8);
  p.filt_cutoff = 0xFFF;
  p.filt_mode = SYNTH_FLT_LP;
  p.filt_ad = SynthAd(0, 0);
  p.filt_sr = SynthSr(0xF, 8);
  p.volume = 0xC0;
  return p;
}

namespace {

VoiceParams MakeFmBass() {
  VoiceParams p = DefaultVoiceParams();
  p.algorithm = 0;
  p.feedback = 2;
  p.feedback_op = 1;
  p.op1_wave = SYNTH_WAVE_SINE;
  p.op2_wave = SYNTH_WAVE_SINE;
  p.op3_wave = SYNTH_WAVE_SINE;
  p.op1_level = 0xFF;
  p.op2_level = 0xC0;
  p.op3_level = 0x80;
  p.op2_ratio = 1;
  p.op3_ratio = 2;
  p.op1_ad = SynthAd(0, 2);
  p.op1_sr = SynthSr(0xF, 6);
  p.op2_ad = SynthAd(0, 1);
  p.op2_sr = SynthSr(0xF, 5);
  p.op3_ad = SynthAd(0, 1);
  p.op3_sr = SynthSr(0xF, 5);
  p.filt_cutoff = 0x600;
  p.filt_reso = 0x4;
  p.filt_mode = SYNTH_FLT_LP;
  p.volume = 0xC0;
  return p;
}

VoiceParams MakeSawPad() {
  VoiceParams p = DefaultVoiceParams();
  p.algorithm = 4;
  p.op1_wave = SYNTH_WAVE_SAW;
  p.op2_wave = SYNTH_WAVE_SAW;
  p.op2_ratio = 1;
  p.op2_detune = 5;
  p.op2_level = 0x60;
  p.op3_wave = SYNTH_WAVE_SAW;
  p.op3_ratio = 2;
  p.op3_detune = -7;
  p.op3_level = 0x40;
  p.op1_ad = SynthAd(1, 4);
  p.op1_sr = SynthSr(0xE, 9);
  p.filt_cutoff = 0x500;
  p.filt_reso = 0x2;
  p.lfo_rate = 0x30;
  p.lfo_depth = 0x40;
  p.lfo_target = SYNTH_LFO_TGT_CUTOFF;
  p.volume = 0xA0;
  return p;
}

static const NoteEvent kArpCeg[] = {
    {0.0f, 60, true, false},
    {0.4f, 64, true, false},
    {0.8f, 67, true, false},
    {1.2f, 72, true, false},
    {1.6f, 0, false, true},
};

static const NoteEvent kLegatoSlide[] = {
    {0.0f, 60, true, false},
    {0.8f, 67, false, false},
    {1.6f, 72, false, false},
    {2.4f, 0, false, true},
};

struct SynthPresetEntry {
  const char *name;
  const char *description;
  uint8_t default_note;
  float release_at;
  VoiceParams (*make_params)();
  const NoteEvent *sequence;
  size_t sequence_len;
};

SynthPresetEntry gEntries[] = {
    {"default", "Plain sine additive", 60, 0.f, DefaultVoiceParams, nullptr, 0},
    {"fm_bass", "FM bass algorithm 0", 48, 2.f, MakeFmBass, nullptr, 0},
    {"saw_pad", "Detuned saw pad", 60, 0.f, MakeSawPad, nullptr, 0},
    {"arp_ceg", "C major arpeggio sequence", 60, 0.f, DefaultVoiceParams,
     kArpCeg, sizeof(kArpCeg) / sizeof(kArpCeg[0])},
    {"legato_slide", "Legato pitch slide sequence", 60, 0.f, DefaultVoiceParams,
     kLegatoSlide, sizeof(kLegatoSlide) / sizeof(kLegatoSlide[0])},
};

SynthPreset gResolved[sizeof(gEntries) / sizeof(gEntries[0])];
bool gResolvedReady = false;

void EnsureResolved() {
  if (gResolvedReady) {
    return;
  }
  for (size_t i = 0; i < sizeof(gEntries) / sizeof(gEntries[0]); ++i) {
    gResolved[i].name = gEntries[i].name;
    gResolved[i].description = gEntries[i].description;
    gResolved[i].default_note = gEntries[i].default_note;
    gResolved[i].release_at = gEntries[i].release_at;
    gResolved[i].params = gEntries[i].make_params();
    gResolved[i].sequence = gEntries[i].sequence;
    gResolved[i].sequence_len = gEntries[i].sequence_len;
  }
  gResolvedReady = true;
}

} // namespace

const SynthPreset *FindSynthPreset(const char *name) {
  if (!name) {
    return nullptr;
  }
  EnsureResolved();
  for (size_t i = 0; i < sizeof(gResolved) / sizeof(gResolved[0]); ++i) {
    if (std::strcmp(gResolved[i].name, name) == 0) {
      return &gResolved[i];
    }
  }
  return nullptr;
}

const SynthPreset *GetSynthPresets(size_t *count) {
  EnsureResolved();
  if (count) {
    *count = sizeof(gResolved) / sizeof(gResolved[0]);
  }
  return gResolved;
}
