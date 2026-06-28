/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "SynthVoice.h"
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Tuning constants (see docs/synth-design-spec.md). These are deliberately
// grouped so the engine can be re-balanced without hunting through the code.
// ---------------------------------------------------------------------------
namespace {

constexpr int kSampleRate = 44100;
constexpr int kSineBits = 10;            // 1024-entry sine table
constexpr int kSineSize = 1 << kSineBits; // 1024
constexpr int kWaveAmp = 8192;           // operator output amplitude (+/-)

// How hard a modulator drives a carrier's phase. Higher = more aggressive FM.
constexpr int kFMShift = 17;
// Self-feedback strength per feedback step (0-7).
constexpr int kFBShift = 12;
// Final output headroom shift applied before clipping to int16.
constexpr int kOutShift = 0;

// Lookup tables -------------------------------------------------------------
int16_t sineTable[kSineSize];
uint32_t noteInc[128];   // phase increment (Q32) per MIDI note
uint16_t envRate[16];    // envelope level delta per sample
int32_t cutoffCoef[256]; // SVF frequency coefficient f (Q15)
int32_t resoCoef[16];    // SVF damping coefficient q1 (Q15)
bool tablesReady = false;

inline int32_t clampInt(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Raw oscillator value in [-kWaveAmp, kWaveAmp] for a 32-bit phase.
inline int32_t osc(uint32_t phase, uint8_t wave, uint16_t pw,
                   uint32_t &noiseState) {
  switch (wave) {
  case SYNTH_WAVE_SINE:
    return sineTable[phase >> (32 - kSineBits)];
  case SYNTH_WAVE_TRI: {
    // triangle from top 16 bits
    int32_t t = (int32_t)(phase >> 16); // 0..65535
    int32_t tri = (t < 32768) ? (t * 2 - 32768) : (98304 - t * 2);
    return (tri * kWaveAmp) >> 15;
  }
  case SYNTH_WAVE_SAW: {
    int32_t s = (int32_t)(phase >> 16) - 32768; // -32768..32767
    return (s * kWaveAmp) >> 15;
  }
  case SYNTH_WAVE_PULSE: {
    uint32_t threshold = (uint32_t)pw << 20; // pw is 12-bit
    return (phase < threshold) ? kWaveAmp : -kWaveAmp;
  }
  case SYNTH_WAVE_NOISE: {
    // 23-bit Galois LFSR, poly x^23 + x^18 + 1
    uint32_t lfsr = noiseState;
    uint32_t bit = ((lfsr >> 22) ^ (lfsr >> 17)) & 1;
    lfsr = ((lfsr << 1) | bit) & 0x7FFFFF;
    noiseState = lfsr;
    return ((int32_t)(lfsr & 0xFFF) - 2048) * kWaveAmp >> 11;
  }
  default:
    return 0;
  }
}

// Advance an ADSR envelope one sample. Returns the new level.
inline uint16_t advanceEnv(uint8_t &stage, uint16_t level, uint8_t ad,
                           uint8_t sr) {
  uint8_t attack = ad >> 4;
  uint8_t decay = ad & 0xF;
  uint8_t sustain = sr >> 4;
  uint8_t release = sr & 0xF;
  uint16_t susLevel = (uint16_t)sustain * 0x1111;
  int32_t l = level;
  switch (stage) {
  case ENV_ATTACK:
    l += envRate[attack];
    if (l >= 0xFFFF) {
      l = 0xFFFF;
      stage = ENV_DECAY;
    }
    break;
  case ENV_DECAY:
    l -= envRate[decay];
    if (l <= susLevel) {
      l = susLevel;
      stage = ENV_SUSTAIN;
    }
    break;
  case ENV_SUSTAIN:
    l = susLevel;
    break;
  case ENV_RELEASE:
    l -= envRate[release];
    if (l <= 0) {
      l = 0;
      stage = ENV_OFF;
    }
    break;
  default:
    l = 0;
    break;
  }
  return (uint16_t)l;
}

// Advance the AD-only pitch envelope (attack to full, decay back to 0).
inline uint16_t advancePitchEnv(uint8_t &stage, uint16_t level, uint8_t ad) {
  uint8_t attack = ad >> 4;
  uint8_t decay = ad & 0xF;
  int32_t l = level;
  switch (stage) {
  case ENV_ATTACK:
    l += envRate[attack];
    if (l >= 0xFFFF) {
      l = 0xFFFF;
      stage = ENV_DECAY;
    }
    break;
  case ENV_DECAY:
    l -= envRate[decay];
    if (l <= 0) {
      l = 0;
      stage = ENV_OFF;
    }
    break;
  default:
    l = 0;
    break;
  }
  return (uint16_t)l;
}

// LFO value in [-32768, 32767] for a given phase.
inline int32_t lfoValue(uint32_t phase, uint8_t shape, uint32_t &shLatch,
                        uint32_t &noiseState) {
  switch (shape) {
  case SYNTH_LFO_SINE:
    return ((int32_t)sineTable[phase >> (32 - kSineBits)] << 15) / kWaveAmp;
  case SYNTH_LFO_TRI: {
    int32_t t = (int32_t)(phase >> 16);
    return (t < 32768) ? (t * 2 - 32768) : (98304 - t * 2);
  }
  case SYNTH_LFO_SAW:
    return (int32_t)(phase >> 16) - 32768;
  case SYNTH_LFO_SQUARE:
    return (phase < 0x80000000u) ? 32767 : -32768;
  case SYNTH_LFO_SH:
    return (int32_t)shLatch - 32768;
  default:
    return 0;
  }
}

} // namespace

void SynthVoice::InitTables() {
  if (tablesReady)
    return;

  for (int i = 0; i < kSineSize; i++) {
    sineTable[i] =
        (int16_t)lrintf(sinf(2.0f * (float)M_PI * i / kSineSize) * kWaveAmp);
  }

  for (int n = 0; n < 128; n++) {
    float hz = 440.0f * powf(2.0f, (n - 69) / 12.0f);
    double inc = (double)hz / kSampleRate * 4294967296.0; // 2^32
    noteInc[n] = (uint32_t)inc;
  }

  // Envelope rates: nibble 0 = ~2ms, nibble 15 = ~6s (exponential).
  for (int i = 0; i < 16; i++) {
    float seconds = 0.002f * powf(6000.0f / 2.0f, i / 15.0f) / 1000.0f;
    float samples = seconds * kSampleRate;
    int rate = (int)(65535.0f / samples);
    envRate[i] = (uint16_t)clampInt(rate, 1, 65535);
  }

  // SVF cutoff coefficient table (exponential 20Hz .. fs/6), f = 2*sin(pi*fc/fs)
  float fcMax = kSampleRate / 6.0f;
  for (int i = 0; i < 256; i++) {
    float fc = 20.0f * powf(fcMax / 20.0f, i / 255.0f);
    float f = 2.0f * sinf((float)M_PI * fc / kSampleRate);
    cutoffCoef[i] = (int32_t)lrintf(f * 32768.0f);
  }

  // SVF damping: high resonance -> low damping (towards self oscillation).
  for (int i = 0; i < 16; i++) {
    float q = 0.5f + i * 0.9f;          // Q from 0.5 .. ~14
    float damp = 1.0f / q;              // damping
    int32_t q1 = (int32_t)lrintf(damp * 32768.0f);
    resoCoef[i] = clampInt(q1, 256, 32768);
  }

  tablesReady = true;
}

uint32_t SynthVoice::NoteToInc(uint8_t note) {
  if (note > 127)
    note = 127;
  return noteInc[note];
}

void SynthVoice::Reset() {
  memset(this, 0, sizeof(SynthVoice));
  noise_state = 0x7A5BC3; // non-zero LFSR seed
}

void SynthVoice::Trigger(const VoiceParams &p, uint32_t baseInc, uint8_t n,
                         uint8_t vel, bool retrigger) {
  params = p;
  note = n;
  velocity = vel;
  if (noise_state == 0)
    noise_state = 0x7A5BC3; // non-zero LFSR seed

  porta_target = baseInc;
  if (retrigger || params.porta_rate == 0 || porta_current == 0) {
    porta_current = baseInc;
  }

  // op2/op3 increment ratios (ratio multiplier + cent detune), Q16
  float ratio2 = (params.op2_ratio == 0) ? 0.5f : (float)params.op2_ratio;
  float ratio3 = (params.op3_ratio == 0) ? 0.5f : (float)params.op3_ratio;
  ratio2 *= powf(2.0f, params.op2_detune / 1200.0f);
  ratio3 *= powf(2.0f, params.op3_detune / 1200.0f);
  op2_inc_ratio = (uint32_t)(ratio2 * 65536.0f);
  op3_inc_ratio = (uint32_t)(ratio3 * 65536.0f);

  if (retrigger) {
    op1_phase = op2_phase = op3_phase = 0;
    op1_env_stage = op2_env_stage = op3_env_stage = ENV_ATTACK;
    op1_env_level = op2_env_level = op3_env_level = 0;
    filt_env_stage = ENV_ATTACK;
    filt_env_level = 0;
    pitch_env_stage = (params.pitch_ad != 0) ? ENV_ATTACK : ENV_OFF;
    pitch_env_level = 0;
    filt_lp = filt_bp = 0;
    lfo_phase = 0;
    lfo_delay_timer = params.lfo_delay;
    fb_history[0] = fb_history[1] = 0;
    voice_flags = SYNTH_VF_PLAYING;
  } else {
    voice_flags |= SYNTH_VF_PLAYING | SYNTH_VF_LEGATO;
  }
}

void SynthVoice::Release() {
  op1_env_stage = ENV_RELEASE;
  op2_env_stage = ENV_RELEASE;
  op3_env_stage = ENV_RELEASE;
  filt_env_stage = ENV_RELEASE;
}

bool SynthVoice::IsActive() const {
  if (!(voice_flags & SYNTH_VF_PLAYING))
    return false;
  // Output ops are the carriers; once they are all OFF there is no sound.
  return !(op1_env_stage == ENV_OFF && op2_env_stage == ENV_OFF &&
           op3_env_stage == ENV_OFF);
}

bool SynthVoice::RenderBlock(fixed *buffer, int size) {
  if (!IsActive()) {
    if (voice_flags & SYNTH_VF_PLAYING)
      voice_flags = 0; // release tail finished, stop spending cycles
    memset(buffer, 0, size * 2 * sizeof(fixed));
    return false;
  }

  const VoiceParams &p = params;
  const uint8_t algo = p.algorithm;
  const int volume = p.volume;
  const int subLevel = p.sub_level;
  const bool ringMod = (p.mod_flags & SYNTH_MF_RINGMOD);
  const bool hardSync = (p.mod_flags & SYNTH_MF_HARDSYNC);

  // LFO phase increment: rate 0-255 mapped ~0.05Hz .. ~20Hz.
  float lfoHz = 0.05f + (p.lfo_rate / 255.0f) * 20.0f;
  uint32_t lfoInc = (uint32_t)((double)lfoHz / kSampleRate * 4294967296.0);

  // portamento per-sample glide step on the op1 increment
  uint32_t portaStep = 0;
  if (p.porta_rate > 0) {
    portaStep = ((uint32_t)(256 - p.porta_rate)) << 6;
  }

  for (int i = 0; i < size; i++) {
    // --- glide op1 increment towards target ---
    if (porta_current != porta_target && portaStep) {
      if (porta_current < porta_target) {
        porta_current += portaStep;
        if (porta_current > porta_target)
          porta_current = porta_target;
      } else {
        porta_current -= portaStep;
        if (porta_current < porta_target)
          porta_current = porta_target;
      }
    } else {
      porta_current = porta_target;
    }

    // --- advance modulators ---
    uint32_t lfoPrev = lfo_phase;
    lfo_phase += lfoInc;
    if (lfo_delay_timer > 0)
      lfo_delay_timer--;
    // refresh S&H latch when the LFO phase wraps to a new period
    if (p.lfo_shape == SYNTH_LFO_SH && lfo_phase < lfoPrev)
      lfo_sh_value = (noise_state & 0xFFFF);

    int32_t lfoRaw =
        lfoValue(lfo_phase, p.lfo_shape, lfo_sh_value, noise_state);
    int lfoDepth = p.lfo_depth;
    if (lfo_delay_timer > 0)
      lfoDepth = 0;
    // scaled lfo in [-lfoDepth, lfoDepth] range, Q?  (lfoRaw/32768 * depth)
    int32_t lfoScaled = (lfoRaw * lfoDepth) >> 8; // ~[-32768,32767]*depth/256

    pitch_env_level = advancePitchEnv(pitch_env_stage, pitch_env_level, p.pitch_ad);

    // --- pitch modulation -> increment multiplier (Q16) ---
    // pitch env (semitones) + lfo (if targeting pitch)
    float semis = (p.pitch_depth * (pitch_env_level / 65535.0f));
    if (p.lfo_target == SYNTH_LFO_TGT_PITCH) {
      semis += (lfoScaled / 32768.0f) * 2.0f; // up to ~2 semitones
    }
    uint32_t pitchMul = 65536;
    if (semis != 0.0f)
      pitchMul = (uint32_t)(powf(2.0f, semis / 12.0f) * 65536.0f);

    uint32_t inc1 = (uint32_t)(((uint64_t)porta_current * pitchMul) >> 16);
    uint32_t inc2 = (uint32_t)(((uint64_t)inc1 * op2_inc_ratio) >> 16);
    uint32_t inc3 = (uint32_t)(((uint64_t)inc1 * op3_inc_ratio) >> 16);

    // --- advance operator envelopes ---
    op1_env_level = advanceEnv(op1_env_stage, op1_env_level, p.op1_ad, p.op1_sr);
    op2_env_level = advanceEnv(op2_env_stage, op2_env_level, p.op2_ad, p.op2_sr);
    op3_env_level = advanceEnv(op3_env_stage, op3_env_level, p.op3_ad, p.op3_sr);
    filt_env_level =
        advanceEnv(filt_env_stage, filt_env_level, p.filt_ad, p.filt_sr);

    // --- feedback phase mod (applied to feedback_op) ---
    int32_t fbAvg = (fb_history[0] + fb_history[1]) >> 1;
    int32_t fbPhase = p.feedback ? (fbAvg * p.feedback) << (kFBShift - 8) : 0;

    // pulse width modulation from lfo
    uint16_t pw = p.op1_pw;
    if (p.lfo_target == SYNTH_LFO_TGT_PW) {
      int32_t m = (int32_t)pw + (lfoScaled >> 4);
      pw = (uint16_t)clampInt(m, 0, 0xFFF);
    }

    // extra FM depth from lfo
    int32_t fmLfo = (p.lfo_target == SYNTH_LFO_TGT_FM) ? (lfoScaled << 6) : 0;

    // --- compute operators (op3 first since it is always a modulator/source) ---
    int32_t fb3 = (p.feedback_op == 2) ? fbPhase : 0;
    int32_t o3raw = osc(op3_phase + fb3, p.op3_wave, 0, noise_state);
    int32_t o3 = (o3raw * op3_env_level) >> 16;       // post-env
    int32_t o3out = (o3 * p.op3_level) >> 8;          // post-level

    int32_t mod3 = ((int64_t)o3out * ((1 << kFMShift) + fmLfo)) >> 8;

    // op2
    int32_t fb2 = (p.feedback_op == 1) ? fbPhase : 0;
    int32_t mod2in = 0;
    if (algo == 0)
      mod2in = mod3; // 3->2
    int32_t o2raw =
        osc(op2_phase + fb2 + (uint32_t)mod2in, p.op2_wave, 0, noise_state);
    int32_t o2 = (o2raw * op2_env_level) >> 16;
    int32_t o2out = (o2 * p.op2_level) >> 8;
    int32_t mod2 = ((int64_t)o2out * ((1 << kFMShift) + fmLfo)) >> 8;

    // op1 (carrier / main output osc, supports noise + pw)
    int32_t fb1 = (p.feedback_op == 0) ? fbPhase : 0;
    int32_t mod1in = 0;
    switch (algo) {
    case 0:
      mod1in = mod2; // 3->2->1
      break;
    case 1:
      mod1in = mod2 + mod3; // (2+3)->1
      break;
    case 2:
      mod1in = mod3; // 3->1 (+2 clean)
      break;
    case 3:
      mod1in = mod3; // 3->1, 3->2
      break;
    case 4:
    default:
      mod1in = 0; // additive
      break;
    }
    if (hardSync && (op2_phase + inc2) < op2_phase) {
      op1_phase = 0; // op1 reset on op2 wrap
    }
    int32_t o1raw =
        osc(op1_phase + fb1 + (uint32_t)mod1in, p.op1_wave, pw, noise_state);
    int32_t o1 = (o1raw * op1_env_level) >> 16;
    int32_t o1out = (o1 * p.op1_level) >> 8;

    // feedback history is taken from the feedback operator's pre-level output
    int32_t fbSource = (p.feedback_op == 2) ? o3 : (p.feedback_op == 1 ? o2 : o1);
    fb_history[1] = fb_history[0];
    fb_history[0] = fbSource;

    // ring modulation op1 * op2
    if (ringMod)
      o1out = (o1out * o2out) >> 11;

    // --- mix carriers per algorithm ---
    int32_t mix = 0;
    switch (algo) {
    case 0: // series -> only op1 is carrier
      mix = o1out;
      break;
    case 1: // (2+3)->1 -> op1 carrier
      mix = o1out;
      break;
    case 2: // 3->1 + 2 -> op1 and op2 carriers
      mix = o1out + o2out;
      break;
    case 3: // 3->1,3->2 -> op1 + op2 carriers
      mix = o1out + o2out;
      break;
    case 4: // additive
    default:
      mix = o1out + o2out + o3out;
      break;
    }

    // sub oscillator: op1 an octave down (phase/2), cheap, no extra state
    if (subLevel) {
      int32_t subRaw = osc(op1_phase >> 1, SYNTH_WAVE_SINE, 0, noise_state);
      mix += ((subRaw * op1_env_level) >> 16) * subLevel >> 8;
    }

    // --- state variable filter ---
    int cutoffIdx = (p.filt_cutoff >> 4) & 0xFF;
    // key tracking: shift cutoff up with note
    if (p.filt_keytrack)
      cutoffIdx += ((note - 60) * p.filt_keytrack) >> 4;
    // filter envelope modulation
    if (p.filt_env_depth)
      cutoffIdx += (p.filt_env_depth * (filt_env_level >> 8)) >> 8;
    // lfo -> cutoff
    if (p.lfo_target == SYNTH_LFO_TGT_CUTOFF)
      cutoffIdx += lfoScaled >> 9;
    cutoffIdx = clampInt(cutoffIdx, 0, 255);

    int32_t f = cutoffCoef[cutoffIdx];
    int32_t q1 = resoCoef[p.filt_reso & 0xF];

    filt_lp += (f * filt_bp) >> 15;
    int32_t hp = mix - filt_lp - ((q1 * filt_bp) >> 15);
    filt_bp += (f * hp) >> 15;

    int32_t filtered;
    switch (p.filt_mode) {
    case SYNTH_FLT_LP:
      filtered = filt_lp;
      break;
    case SYNTH_FLT_BP:
      filtered = filt_bp;
      break;
    case SYNTH_FLT_HP:
      filtered = hp;
      break;
    case SYNTH_FLT_NOTCH:
    default:
      filtered = filt_lp + hp;
      break;
    }

    // --- amplitude (volume + optional tremolo) ---
    int32_t amp = volume;
    if (p.lfo_target == SYNTH_LFO_TGT_AMP) {
      amp = volume - ((volume * (32767 - lfoRaw) >> 16) * lfoDepth >> 8);
      amp = clampInt(amp, 0, 255);
    }
    int32_t out = (filtered * amp) >> 8;

    // --- advance phases ---
    op1_phase += inc1;
    op2_phase += inc2;
    op3_phase += inc3;

    // headroom + clip
    out >>= kOutShift;
    out = clampInt(out, -32768, 32767);

    fixed fp = i2fp(out);
    buffer[2 * i] = fp;
    buffer[2 * i + 1] = fp;
  }

  return IsActive();
}
