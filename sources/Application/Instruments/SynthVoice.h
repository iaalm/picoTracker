/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * 3-operator hybrid (FM + subtractive) synthesizer voice.
 * See docs/synth-design-spec.md for the full design.
 *
 * A SynthVoice is a fully self-contained runtime state for one channel. On
 * Trigger() it caches every parameter it needs out of the instrument's
 * Variables; RenderBlock() then reads ONLY voice state, never the instrument,
 * which guarantees zero cross-channel / cross-instrument interference.
 */

#ifndef _SYNTH_VOICE_H_
#define _SYNTH_VOICE_H_

#include "Application/Utils/fixed.h"
#include <cstdint>

// Waveform ids for operator 1 (op2/op3 cannot use noise)
enum SynthWave {
  SYNTH_WAVE_SINE = 0,
  SYNTH_WAVE_TRI,
  SYNTH_WAVE_SAW,
  SYNTH_WAVE_PULSE,
  SYNTH_WAVE_NOISE,
  SYNTH_WAVE_LAST
};

enum SynthLFOShape {
  SYNTH_LFO_SINE = 0,
  SYNTH_LFO_TRI,
  SYNTH_LFO_SAW,
  SYNTH_LFO_SQUARE,
  SYNTH_LFO_SH,
  SYNTH_LFO_LAST
};

enum SynthLFOTarget {
  SYNTH_LFO_TGT_PITCH = 0,
  SYNTH_LFO_TGT_CUTOFF,
  SYNTH_LFO_TGT_PW,
  SYNTH_LFO_TGT_FM,
  SYNTH_LFO_TGT_AMP,
  SYNTH_LFO_TGT_LAST
};

enum SynthFilterMode {
  SYNTH_FLT_LP = 0,
  SYNTH_FLT_BP,
  SYNTH_FLT_HP,
  SYNTH_FLT_NOTCH,
  SYNTH_FLT_LAST
};

enum SynthEnvStage {
  ENV_OFF = 0,
  ENV_ATTACK,
  ENV_DECAY,
  ENV_SUSTAIN,
  ENV_RELEASE
};

// Voice control flags (runtime)
#define SYNTH_VF_PLAYING 0x01
#define SYNTH_VF_LEGATO 0x02

// Cached instrument flags (config)
#define SYNTH_MF_HARDSYNC 0x01
#define SYNTH_MF_RINGMOD 0x02

// Instrument params cached into the voice on Trigger().
struct VoiceParams {
  // routing
  uint8_t algorithm;   // 0-4 connection topology
  uint8_t feedback;    // 0-7 self-feedback amount
  uint8_t feedback_op; // 0-2 which op self-modulates

  // oscillators
  uint8_t op1_wave; // 0-4
  uint8_t op2_wave; // 0-3
  uint8_t op3_wave; // 0-3
  uint16_t op1_pw;  // 0x000-0xFFF
  uint8_t op1_level;
  uint8_t op2_level;
  uint8_t op3_level;
  uint8_t op2_ratio; // 0=0.5x, 1=1x .. 255=255x
  uint8_t op3_ratio;
  int8_t op2_detune; // cents -64..+63
  int8_t op3_detune;

  // packed ADSR (hi nibble | lo nibble): _ad = A|D, _sr = S|R
  uint8_t op1_ad, op1_sr;
  uint8_t op2_ad, op2_sr;
  uint8_t op3_ad, op3_sr;
  uint8_t filt_ad, filt_sr;
  uint8_t pitch_ad; // A|D (no sustain)

  // filter
  uint16_t filt_cutoff; // 0x000-0xFFF
  uint8_t filt_reso;    // 0-F
  uint8_t filt_mode;    // LP/BP/HP/Notch
  uint8_t filt_keytrack;
  int8_t filt_env_depth; // -128..+127

  // pitch env
  int8_t pitch_depth; // semitones -128..+127

  // lfo
  uint8_t lfo_rate;
  uint8_t lfo_shape;
  uint8_t lfo_depth;
  uint8_t lfo_target;
  uint8_t lfo_delay;

  // misc
  uint8_t porta_rate;
  uint8_t volume;
  uint8_t sub_level;
  uint8_t mod_flags; // hard_sync / ring_mod
};

struct SynthVoice {
  // --- oscillators ---
  uint32_t op1_phase;
  uint32_t op2_phase;
  uint32_t op3_phase;
  // Sub oscillator, one octave below op1. It needs its own accumulator:
  // shifting op1_phase right only compresses the phase into half the table
  // and snaps back every op1 cycle, which is the same pitch plus a
  // discontinuity, not an octave down.
  uint32_t sub_phase;
  uint32_t noise_state; // 23-bit LFSR

  // --- operator envelopes ---
  uint16_t op1_env_level, op2_env_level, op3_env_level;
  uint8_t op1_env_stage, op2_env_stage, op3_env_stage;

  // --- filter envelope ---
  uint16_t filt_env_level;
  uint8_t filt_env_stage;

  // --- pitch envelope (AD only) ---
  uint16_t pitch_env_level;
  uint8_t pitch_env_stage;

  // --- state variable filter ---
  int32_t filt_lp;
  int32_t filt_bp;

  // --- lfo ---
  uint32_t lfo_phase;
  uint16_t lfo_delay_timer;
  uint32_t lfo_sh_value; // sample&hold latch

  // --- portamento (op1 phase increment, Q32) ---
  uint32_t porta_current;
  uint32_t porta_target;
  // op2/op3 increment ratios relative to op1 (Q16, includes ratio + detune)
  uint32_t op2_inc_ratio;
  uint32_t op3_inc_ratio;

  // --- fm feedback history ---
  int32_t fb_history[2];

  // --- voice control ---
  uint8_t note;
  uint8_t velocity;
  uint8_t voice_flags;

  // --- cached instrument params ---
  VoiceParams params;

  // Reset all runtime state to silence.
  void Reset();

  // Begin a note. baseInc is op1's phase increment (Q32) for the note.
  // retrigger restarts the envelopes; otherwise this is a legato slide.
  void Trigger(const VoiceParams &p, uint32_t baseInc, uint8_t note,
               uint8_t velocity, bool retrigger);

  // Note off: move envelopes to release.
  void Release();

  // True if the voice is still producing sound (playing or in release tail).
  bool IsActive() const;

  // Render size stereo frames into an interleaved buffer (overwrites it).
  // Returns true if any audio was produced.
  bool RenderBlock(fixed *buffer, int size);

  // Build lookup tables. Safe to call multiple times.
  static void InitTables();

  // Phase increment (Q32) for a MIDI note (0-127).
  static uint32_t NoteToInc(uint8_t note);
};

#endif
