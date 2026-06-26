/*
 * Named VoiceParams presets for synth_render.
 */
#pragma once

#include "Application/Instruments/SynthVoice.h"

#include <cstddef>

inline constexpr uint8_t SynthAd(uint8_t attack, uint8_t decay) {
  return static_cast<uint8_t>(((attack & 0xF) << 4) | (decay & 0xF));
}

inline constexpr uint8_t SynthSr(uint8_t sustain, uint8_t release) {
  return static_cast<uint8_t>(((sustain & 0xF) << 4) | (release & 0xF));
}

struct NoteEvent {
  float at_seconds = 0.f;
  uint8_t note = 60;
  bool retrigger = true;
  bool gate_off = false;
};

struct SynthPreset {
  const char *name = nullptr;
  const char *description = nullptr;
  uint8_t default_note = 60;
  float release_at = 0.f;
  VoiceParams params{};
  const NoteEvent *sequence = nullptr;
  size_t sequence_len = 0;
};

VoiceParams DefaultVoiceParams();

const SynthPreset *FindSynthPreset(const char *name);
const SynthPreset *GetSynthPresets(size_t *count);
