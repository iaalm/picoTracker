/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * SynthVoice (KX1) DSP regression tests.
 *
 * These lock behaviour that is invisible to the persistence tests because it
 * only shows up once the voice actually renders: an oscillator that is not at
 * the pitch it claims, a modulator that never reaches the carrier, an LFO
 * stuck on a constant, or a voice that never reports itself finished.
 *
 * Every check here renders through the real RenderBlock().
 */

#include "Application/Instruments/SynthVoice.h"
#include "doctest/doctest.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;

// A plain, audible patch: single sine carrier, instant attack, full sustain.
VoiceParams basePatch() {
  VoiceParams p{};
  p.algorithm = 4; // additive, no FM in the way
  p.op1_wave = SYNTH_WAVE_SINE;
  p.op2_wave = SYNTH_WAVE_SINE;
  p.op3_wave = SYNTH_WAVE_SINE;
  p.op1_level = 0xFF;
  p.volume = 0xFF;
  p.op1_ad = 0x00;
  p.op1_sr = 0xF0; // sustain F, release 0
  p.op2_ad = 0x00;
  p.op2_sr = 0xF0;
  p.op3_ad = 0x00;
  p.op3_sr = 0xF0;
  p.filt_cutoff = 0xFFF;
  p.filt_mode = SYNTH_FLT_LP;
  p.filt_ad = 0x00;
  p.filt_sr = 0xF0;
  return p;
}

// Render `seconds` of mono samples from a freshly triggered voice.
std::vector<double> render(const VoiceParams &p, uint8_t note, double seconds) {
  SynthVoice::InitTables();
  SynthVoice v;
  v.Reset();
  v.Trigger(p, SynthVoice::NoteToInc(note), note, 0x7F, true);
  const int kBlock = 256;
  std::vector<fixed> buf(kBlock * 2);
  std::vector<double> out;
  int blocks = (int)(seconds * kSampleRate / kBlock);
  for (int b = 0; b < blocks; b++) {
    v.RenderBlock(buf.data(), kBlock);
    for (int i = 0; i < kBlock; i++) {
      out.push_back((double)(buf[2 * i] >> 15));
    }
  }
  return out;
}

// Rising-zero-crossing pitch estimate. Good enough to tell an octave apart.
double estimateHz(const std::vector<double> &x) {
  int crossings = 0;
  for (size_t i = 1; i < x.size(); i++) {
    if (x[i - 1] < 0.0 && x[i] >= 0.0) {
      crossings++;
    }
  }
  return crossings / ((double)x.size() / kSampleRate);
}

double rms(const std::vector<double> &x) {
  double sum = 0;
  for (double v : x) {
    sum += v * v;
  }
  return x.empty() ? 0 : std::sqrt(sum / x.size());
}

// Relative difference between two equal-length renders, in percent.
double relDiffPercent(const std::vector<double> &a,
                      const std::vector<double> &b) {
  REQUIRE(a.size() == b.size());
  double num = 0, den = 0;
  for (size_t i = 0; i < a.size(); i++) {
    double d = a[i] - b[i];
    num += d * d;
    den += a[i] * a[i];
  }
  return den > 0 ? 100.0 * std::sqrt(num / den) : 0.0;
}

} // namespace

// ---------------------------------------------------------------------------
// The sub oscillator must actually be an octave below op1.
//
// It used to read osc(op1_phase >> 1): shifting the phase compresses it into
// half the wavetable and snaps back on every op1 wrap, which is the same
// pitch plus a discontinuity. A real octave needs its own accumulator.
// ---------------------------------------------------------------------------
TEST_CASE("S1: sub oscillator sounds one octave below op1") {
  VoiceParams carrier = basePatch();
  std::vector<double> op1 = render(carrier, 69, 1.5); // A4 = 440 Hz

  VoiceParams sub = basePatch();
  sub.op1_level = 0; // silence the carrier, leave only the sub
  sub.sub_level = 0xFF;
  std::vector<double> only = render(sub, 69, 1.5);

  const double op1Hz = estimateHz(op1);
  const double subHz = estimateHz(only);
  INFO("op1 ", op1Hz, " Hz, sub ", subHz, " Hz");

  CHECK(op1Hz == doctest::Approx(440.0).epsilon(0.02));
  CHECK(subHz == doctest::Approx(220.0).epsilon(0.02));
  CHECK(rms(only) > 0.0); // the sub must be audible on its own
}

// ---------------------------------------------------------------------------
// Sample & hold must keep sampling on every waveform.
//
// The latch was read from noise_state, which osc() only advances for a NOISE
// waveform. On a sine/tri/saw/pulse carrier the latch therefore never changed
// and S&H degenerated into a constant DC offset.
// ---------------------------------------------------------------------------
TEST_CASE("S2: S&H LFO keeps sampling on non-noise waveforms") {
  SynthVoice::InitTables();

  auto distinctLatches = [](uint8_t wave) {
    VoiceParams p = basePatch();
    p.op1_wave = wave;
    p.lfo_shape = SYNTH_LFO_SH;
    p.lfo_rate = 0xFF; // fast, so many periods fit in the window
    p.lfo_depth = 0xFF;
    p.lfo_target = SYNTH_LFO_TGT_CUTOFF;

    SynthVoice v;
    v.Reset();
    v.Trigger(p, SynthVoice::NoteToInc(60), 60, 0x7F, true);

    std::vector<fixed> buf(256 * 2);
    std::vector<uint32_t> seen;
    for (int b = 0; b < 2 * kSampleRate / 256; b++) {
      v.RenderBlock(buf.data(), 256);
      if (std::find(seen.begin(), seen.end(), v.lfo_sh_value) == seen.end()) {
        seen.push_back(v.lfo_sh_value);
      }
    }
    return seen.size();
  };

  // A stuck latch yields 2 (the initial 0 plus one constant re-read).
  CHECK(distinctLatches(SYNTH_WAVE_SINE) > 8);
  CHECK(distinctLatches(SYNTH_WAVE_SAW) > 8);
  CHECK(distinctLatches(SYNTH_WAVE_NOISE) > 8);
}

// ---------------------------------------------------------------------------
// FM has to be audible.
//
// kFMShift was so small that a full-level modulator moved the carrier phase
// by ~0.006 radians. Driving every modulator and the feedback path from zero
// to maximum changed the output by well under 1%, i.e. the FM section — the
// entire point of the instrument — did nothing.
// ---------------------------------------------------------------------------
TEST_CASE("S3: FM modulation audibly changes the carrier") {
  auto fmPatch = [](uint8_t modLevel, uint8_t feedback) {
    VoiceParams p = basePatch();
    p.algorithm = 0; // 3 -> 2 -> 1 series FM
    p.op2_level = modLevel;
    p.op3_level = modLevel;
    p.op2_ratio = 2;
    p.op3_ratio = 3;
    p.feedback = feedback;
    p.feedback_op = 1;
    return p;
  };

  std::vector<double> off = render(fmPatch(0x00, 0), 60, 0.5);
  std::vector<double> on = render(fmPatch(0xFF, 7), 60, 0.5);

  const double diff = relDiffPercent(off, on);
  INFO("modulators off vs full: ", diff, "% RMS difference");
  CHECK(diff > 10.0);

  // Louder modulation must not blow up the output stage.
  for (double s : on) {
    REQUIRE(s >= -32768.0);
    REQUIRE(s <= 32767.0);
  }
}

// ---------------------------------------------------------------------------
// A voice must eventually report itself finished.
//
// IsActive() only looked for ENV_OFF. Any patch with a sustain nibble of 0 —
// every pluck and percussive sound — decays to level 0 and then parks in
// ENV_SUSTAIN, so without a gate-off the voice stayed "active" forever,
// rendering silence and holding its channel.
// ---------------------------------------------------------------------------
TEST_CASE("S4: a silent voice stops reporting itself active") {
  SynthVoice::InitTables();
  std::vector<fixed> buf(256 * 2);

  auto runHeld = [&](uint8_t sustainNibble, double seconds) {
    VoiceParams p = basePatch();
    const uint8_t sr = (uint8_t)((sustainNibble << 4) | 0x2);
    p.op1_ad = 0x02;
    p.op1_sr = sr;
    p.op2_ad = 0x02;
    p.op2_sr = sr;
    p.op3_ad = 0x02;
    p.op3_sr = sr;
    SynthVoice v;
    v.Reset();
    v.Trigger(p, SynthVoice::NoteToInc(60), 60, 0x7F, true);
    for (int b = 0; b < (int)(seconds * kSampleRate / 256); b++) {
      v.RenderBlock(buf.data(), 256);
    }
    return v.IsActive();
  };

  // Decays to silence and is never released: must free itself.
  CHECK_FALSE(runHeld(0x0, 5.0));

  // Still holding a real sustain level: must stay active, or held notes
  // would be cut off mid-sound.
  CHECK(runHeld(0xF, 5.0));

  SUBCASE("release still ends the voice") {
    VoiceParams p = basePatch();
    SynthVoice v;
    v.Reset();
    v.Trigger(p, SynthVoice::NoteToInc(60), 60, 0x7F, true);
    for (int b = 0; b < 100; b++) {
      v.RenderBlock(buf.data(), 256);
    }
    CHECK(v.IsActive());
    v.Release();
    for (int b = 0; b < kSampleRate / 256; b++) {
      v.RenderBlock(buf.data(), 256);
    }
    CHECK_FALSE(v.IsActive());
  }
}
