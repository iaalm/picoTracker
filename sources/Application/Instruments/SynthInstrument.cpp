/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "SynthInstrument.h"
#include "CommandList.h"
#include "System/Profiler/Profiler.h"
#include <cstring>

const char *synthWaveNames[SYNTH_WAVE_LAST] = {"sine", "tri", "saw", "pulse",
                                               "noise"};
const char *synthLFOShapeNames[SYNTH_LFO_LAST] = {"sine", "tri", "saw", "sqr",
                                                  "S&H"};
const char *synthLFOTargetNames[SYNTH_LFO_TGT_LAST] = {
    "pitch", "cutoff", "PW", "FM", "amp"};
const char *synthFilterModeNames[SYNTH_FLT_LAST] = {"LP", "BP", "HP", "Notch"};
const char *synthAlgorithmNames[5] = {
    "[3]>[2]>[1]", "[2+3]>[1]", "[3]>[1]+2", "[3]>1,2", "[1+2+3]"};

SynthVoice SynthInstrument::voices_[SONG_CHANNEL_COUNT];

// ---------------------------------------------------------------------------
// Plan B: static ParamSpec / NAMES / FORMATS tables for SynthInstrument.
//
// Each ParamSpec takes 20 B in flash. 38 specs ≈ 760 B. Persistence formats
// are plain decimal for round-trip compatibility with existing .pti files
// (legacy Variable::GetString() for INT also used "%d"). UI display
// formatting ("%3.3X", "%s" for waveform/algorithm lists) is supplied by
// the UIParamIntVarField's own format string at the call site.
// ---------------------------------------------------------------------------

// ParamSpec positional layout (matches ParamSpec.h):
//   id (FourCC), _pad0 (u8), name_off (u16), format_off (u16),
//   default_ (i32), min (u16), max (u16), step (u8), big_step (u8),
//   _pad1 (u8), _pad2 (u8)
const ParamSpec SynthInstrument::SPECS[SynthInstrument::kParamCount] = {
    // [0] reserved name slot
    {FourCC::InstrumentName, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
    // [1] Algorithm (CHAR_LIST, 5 entries)
    {FourCC::SynthInstrumentAlgorithm, 0, 1, 1, 0, 0, 4, 1, 1, 0, 0},
    // [2] Feedback
    {FourCC::SynthInstrumentFeedback, 0, 2, 1, 0, 0, 7, 1, 1, 0, 0},
    // [3] FeedbackOp
    {FourCC::SynthInstrumentFeedbackOp, 0, 3, 1, 0, 0, 2, 1, 1, 0, 0},
    // [4] Op1Wave (CHAR_LIST)
    {FourCC::SynthInstrumentOp1Wave, 0, 4, 1, 0, 0, SYNTH_WAVE_LAST - 1, 1, 1, 0, 0},
    // [5] Op1PW
    {FourCC::SynthInstrumentOp1PW, 0, 5, 1, 0x800, 0, 0xFFF, 1, 0x10, 0, 0},
    // [6] Op1Level
    {FourCC::SynthInstrumentOp1Level, 0, 6, 1, 0xFF, 0, 0xFF, 1, 0x10, 0, 0},
    // [7] Op1ADSR
    {FourCC::SynthInstrumentOp1ADSR, 0, 7, 1, 0x00F8, 0, 0xFFFF, 1, 1, 0, 0},
    // [8] Op2Wave (CHAR_LIST, 4 entries)
    {FourCC::SynthInstrumentOp2Wave, 0, 8, 1, 0, 0, 3, 1, 1, 0, 0},
    // [9] Op2Ratio
    {FourCC::SynthInstrumentOp2Ratio, 0, 9, 1, 1, 0, 255, 1, 1, 0, 0},
    // [10] Op2Detune (signed cents; DSP reads it as int8_t)
    {FourCC::SynthInstrumentOp2Detune, 0, 10, 1, 0, -64, 63, 1, 4, 0, 0},
    // [11] Op2Level
    {FourCC::SynthInstrumentOp2Level, 0, 11, 1, 0, 0, 0xFF, 1, 0x10, 0, 0},
    // [12] Op2ADSR
    {FourCC::SynthInstrumentOp2ADSR, 0, 12, 1, 0x00F8, 0, 0xFFFF, 1, 1, 0, 0},
    // [13] Op3Wave
    {FourCC::SynthInstrumentOp3Wave, 0, 13, 1, 0, 0, 3, 1, 1, 0, 0},
    // [14] Op3Ratio
    {FourCC::SynthInstrumentOp3Ratio, 0, 14, 1, 1, 0, 255, 1, 1, 0, 0},
    // [15] Op3Detune (signed cents; DSP reads it as int8_t)
    {FourCC::SynthInstrumentOp3Detune, 0, 15, 1, 0, -64, 63, 1, 4, 0, 0},
    // [16] Op3Level
    {FourCC::SynthInstrumentOp3Level, 0, 16, 1, 0, 0, 0xFF, 1, 0x10, 0, 0},
    // [17] Op3ADSR
    {FourCC::SynthInstrumentOp3ADSR, 0, 17, 1, 0x00F8, 0, 0xFFFF, 1, 1, 0, 0},
    // [18] FilterCutoff
    {FourCC::SynthInstrumentFilterCutoff, 0, 18, 1, 0xFFF, 0, 0xFFF, 1, 0x10, 0, 0},
    // [19] FilterResonance
    {FourCC::SynthInstrumentFilterResonance, 0, 19, 1, 0, 0, 0xF, 1, 1, 0, 0},
    // [20] FilterMode (CHAR_LIST)
    {FourCC::SynthInstrumentFilterMode, 0, 20, 1, 0, 0, SYNTH_FLT_LAST - 1, 1, 1, 0, 0},
    // [21] FilterKeytrack
    {FourCC::SynthInstrumentFilterKeytrack, 0, 21, 1, 0, 0, 0xF, 1, 1, 0, 0},
    // [22] FilterEnvDepth (signed; negative = envelope closes the filter)
    {FourCC::SynthInstrumentFilterEnvDepth, 0, 22, 1, 0, -128, 127, 1, 8, 0, 0},
    // [23] FilterADSR
    {FourCC::SynthInstrumentFilterADSR, 0, 23, 1, 0x00F8, 0, 0xFFFF, 1, 1, 0, 0},
    // [24] PitchDepth (signed semitones; negative = downward sweep)
    {FourCC::SynthInstrumentPitchDepth, 0, 24, 1, 0, -128, 127, 1, 8, 0, 0},
    // [25] PitchAD
    {FourCC::SynthInstrumentPitchAD, 0, 25, 1, 0, 0, 0xFF, 1, 1, 0, 0},
    // [26] LFORate
    {FourCC::SynthInstrumentLFORate, 0, 26, 1, 0x40, 0, 0xFF, 1, 0x10, 0, 0},
    // [27] LFOShape (CHAR_LIST)
    {FourCC::SynthInstrumentLFOShape, 0, 27, 1, 0, 0, SYNTH_LFO_LAST - 1, 1, 1, 0, 0},
    // [28] LFODepth
    {FourCC::SynthInstrumentLFODepth, 0, 28, 1, 0, 0, 0xFF, 1, 0x10, 0, 0},
    // [29] LFOTarget (CHAR_LIST, default 1)
    {FourCC::SynthInstrumentLFOTarget, 0, 29, 1, 1, 0, SYNTH_LFO_TGT_LAST - 1, 1, 1, 0, 0},
    // [30] LFODelay
    {FourCC::SynthInstrumentLFODelay, 0, 30, 1, 0, 0, 0xFFFF, 1, 0x10, 0, 0},
    // [31] Portamento
    {FourCC::SynthInstrumentPortamento, 0, 31, 1, 0, 0, 0xFF, 1, 0x10, 0, 0},
    // [32] HardSync (bool)
    {FourCC::SynthInstrumentHardSync, 0, 32, 1, 0, 0, 1, 1, 1, 0, 0},
    // [33] RingMod (bool)
    {FourCC::SynthInstrumentRingMod, 0, 33, 1, 0, 0, 1, 1, 1, 0, 0},
    // [34] SubLevel
    {FourCC::SynthInstrumentSubLevel, 0, 34, 1, 0, 0, 0xFF, 1, 0x10, 0, 0},
    // [35] Volume
    {FourCC::SynthInstrumentVolume, 0, 35, 1, 0xC0, 0, 0xFF, 1, 0x10, 0, 0},
    // [36] Table (default/min -1 == "no table bound"; min must admit the
    // sentinel or SetParamValue clamps every unbound table to 0)
    {FourCC::SynthInstrumentTable, 0, 36, 1, -1, -1, 0x7F, 1, 0x10, 0, 0},
    // [37] TableAutomation (bool)
    {FourCC::SynthInstrumentTableAutomation, 0, 37, 1, 0, 0, 1, 1, 1, 0, 0},
};

const char *const SynthInstrument::NAMES[SynthInstrument::kParamCount] = {
    /*  0 */ "InstrumentName",
    /*  1 */ "SYNALGORITHM",
    /*  2 */ "SYNFEEDBACK",
    /*  3 */ "SYNFEEDBACKOP",
    /*  4 */ "SYNOP1WAVE",
    /*  5 */ "SYNOP1PW",
    /*  6 */ "SYNOP1LEVEL",
    /*  7 */ "SYNOP1ADSR",
    /*  8 */ "SYNOP2WAVE",
    /*  9 */ "SYNOP2RATIO",
    /* 10 */ "SYNOP2DETUNE",
    /* 11 */ "SYNOP2LEVEL",
    /* 12 */ "SYNOP2ADSR",
    /* 13 */ "SYNOP3WAVE",
    /* 14 */ "SYNOP3RATIO",
    /* 15 */ "SYNOP3DETUNE",
    /* 16 */ "SYNOP3LEVEL",
    /* 17 */ "SYNOP3ADSR",
    /* 18 */ "SYNFLTCUTOFF",
    /* 19 */ "SYNFLTRES",
    /* 20 */ "SYNFLTMODE",
    /* 21 */ "SYNFLTKEYTRACK",
    /* 22 */ "SYNFLTENVDEPTH",
    /* 23 */ "SYNFLTADSR",
    /* 24 */ "SYNPITCHDEPTH",
    /* 25 */ "SYNPITCHAD",
    /* 26 */ "SYNLFORATE",
    /* 27 */ "SYNLFOSHAPE",
    /* 28 */ "SYNLFODEPTH",
    /* 29 */ "SYNLFOTARGET",
    /* 30 */ "SYNLFODELAY",
    /* 31 */ "SYNPORTAMENTO",
    /* 32 */ "SYNHARDSYNC",
    /* 33 */ "SYNRINGMOD",
    /* 34 */ "SYNSUBLEVEL",
    /* 35 */ "SYNVOLUME",
    /* 36 */ "SYNTABLE",
    /* 37 */ "SYNTABLEAUTO",
};

const char *const SynthInstrument::FORMATS[SynthInstrument::kParamCount] = {
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
};

SynthInstrument::SynthInstrument() : I_Instrument(nullptr) {
  // Initialise the packed array to each ParamSpec's default. Mirrors the
  // legacy Variable member initialisers in the pre-stage-3 constructor.
  for (int i = 0; i < kParamCount; i++) {
    params_[i] = SPECS[i].default_;
  }
  // ParamSpec::default_ is uint16_t; the legacy Variable initialised the
  // Table with -1. Re-apply the unbound sentinel explicitly.
  params_[PARAM_TABLE] = -1;
}

SynthInstrument::~SynthInstrument() {}

bool SynthInstrument::Init() {
  SynthVoice::InitTables();
  tableState_.Reset();
  return true;
}

void SynthInstrument::OnStart() { tableState_.Reset(); }

void SynthInstrument::buildParams(VoiceParams &p) {
  p.algorithm = params_[PARAM_ALGORITHM];
  p.feedback = params_[PARAM_FEEDBACK];
  p.feedback_op = params_[PARAM_FEEDBACK_OP];

  p.op1_wave = params_[PARAM_OP1_WAVE];
  p.op2_wave = params_[PARAM_OP2_WAVE];
  p.op3_wave = params_[PARAM_OP3_WAVE];
  p.op1_pw = params_[PARAM_OP1_PW];
  p.op1_level = params_[PARAM_OP1_LEVEL];
  p.op2_level = params_[PARAM_OP2_LEVEL];
  p.op3_level = params_[PARAM_OP3_LEVEL];
  p.op2_ratio = params_[PARAM_OP2_RATIO];
  p.op3_ratio = params_[PARAM_OP3_RATIO];
  p.op2_detune = (int8_t)params_[PARAM_OP2_DETUNE];
  p.op3_detune = (int8_t)params_[PARAM_OP3_DETUNE];

  uint16_t a1 = params_[PARAM_OP1_ADSR];
  uint16_t a2 = params_[PARAM_OP2_ADSR];
  uint16_t a3 = params_[PARAM_OP3_ADSR];
  uint16_t af = params_[PARAM_FLT_ADSR];
  p.op1_ad = a1 >> 8;
  p.op1_sr = a1 & 0xFF;
  p.op2_ad = a2 >> 8;
  p.op2_sr = a2 & 0xFF;
  p.op3_ad = a3 >> 8;
  p.op3_sr = a3 & 0xFF;
  p.filt_ad = af >> 8;
  p.filt_sr = af & 0xFF;
  p.pitch_ad = params_[PARAM_PITCH_AD] & 0xFF;

  p.filt_cutoff = params_[PARAM_FLT_CUTOFF];
  p.filt_reso = params_[PARAM_FLT_RESO];
  p.filt_mode = params_[PARAM_FLT_MODE];
  p.filt_keytrack = params_[PARAM_FLT_KEYTRACK];
  p.filt_env_depth = (int8_t)params_[PARAM_FLT_ENV_DEPTH];

  p.pitch_depth = (int8_t)params_[PARAM_PITCH_DEPTH];

  p.lfo_rate = params_[PARAM_LFO_RATE];
  p.lfo_shape = params_[PARAM_LFO_SHAPE];
  p.lfo_depth = params_[PARAM_LFO_DEPTH];
  p.lfo_target = params_[PARAM_LFO_TARGET];
  p.lfo_delay = params_[PARAM_LFO_DELAY];

  p.porta_rate = params_[PARAM_PORTAMENTO];
  p.volume = params_[PARAM_VOLUME];
  p.sub_level = params_[PARAM_SUB_LEVEL];
  p.mod_flags = (params_[PARAM_HARD_SYNC] ? SYNTH_MF_HARDSYNC : 0) |
                (params_[PARAM_RING_MOD] ? SYNTH_MF_RINGMOD : 0);
}

bool SynthInstrument::Start(int channel, unsigned char note, bool retrigger) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return false;
  VoiceParams p;
  buildParams(p);
  uint32_t baseInc = SynthVoice::NoteToInc(note);
  voices_[channel].Trigger(p, baseInc, note, 0x7F, retrigger);
  return true;
}

void SynthInstrument::Stop(int channel) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return;
  voices_[channel].Release();
}

bool SynthInstrument::Render(int channel, fixed *buffer, int size,
                            bool updateTick) {
  PROFILE_SCOPE("SynthInstrument::Render");
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    memset(buffer, 0, size * 2 * sizeof(fixed));
    return false;
  }
  return voices_[channel].RenderBlock(buffer, size);
}

bool SynthInstrument::IsInitialized() { return true; }

void SynthInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
  switch (cc) {
  case FourCC::InstrumentCommandGateOff:
    if (channel >= 0 && channel < SONG_CHANNEL_COUNT)
      voices_[channel].Release();
    break;
  default:
    break;
  }
}

int SynthInstrument::GetTable() { return params_[PARAM_TABLE]; }

bool SynthInstrument::GetTableAutomation() {
  return params_[PARAM_TABLE_AUTO] != 0;
}

void SynthInstrument::GetTableState(TableSaveState &state) {
  memcpy(state.hopCount_, tableState_.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
  state.groove_ = tableState_.groove_;
}

void SynthInstrument::SetTableState(TableSaveState &state) {
  memcpy(tableState_.hopCount_, state.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
  tableState_.groove_ = state.groove_;
}

// ---------------------------------------------------------------------------
// Plan B: new parameter API overrides for the packed array.
// ---------------------------------------------------------------------------

FourCC SynthInstrument::GetParamID(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return FourCC::Default;
  }
  return SPECS[idx].id;
}

const char *SynthInstrument::GetParamName(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "";
  }
  return NAMES[idx];
}

const char *SynthInstrument::GetParamFormat(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "%d";
  }
  return FORMATS[idx];
}

int SynthInstrument::GetParamMin(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return SPECS[idx].min;
}

int SynthInstrument::GetParamMax(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0xFFFF;
  }
  return SPECS[idx].max;
}

int SynthInstrument::GetParamDefault(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return (int)SPECS[idx].default_;
}

int SynthInstrument::GetParamStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].step;
}

int SynthInstrument::GetParamBigStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].big_step;
}

const I_Instrument::StringParam *
SynthInstrument::StringParams(int &count) const {
  // Counts mirror the pre-packed-storage Variable constructors: op2/op3
  // cannot select noise, so they expose 4 entries rather than SYNTH_WAVE_LAST.
  static const StringParam kStringParams[] = {
      {PARAM_ALGORITHM, synthAlgorithmNames, 5},
      {PARAM_OP1_WAVE, synthWaveNames, SYNTH_WAVE_LAST},
      {PARAM_OP2_WAVE, synthWaveNames, 4},
      {PARAM_OP3_WAVE, synthWaveNames, 4},
      {PARAM_FLT_MODE, synthFilterModeNames, SYNTH_FLT_LAST},
      {PARAM_LFO_SHAPE, synthLFOShapeNames, SYNTH_LFO_LAST},
      {PARAM_LFO_TARGET, synthLFOTargetNames, SYNTH_LFO_TGT_LAST},
      {PARAM_HARD_SYNC, nullptr, 0},   // BOOL
      {PARAM_RING_MOD, nullptr, 0},    // BOOL
      {PARAM_TABLE_AUTO, nullptr, 0},  // BOOL
  };
  count = sizeof(kStringParams) / sizeof(kStringParams[0]);
  return kStringParams;
}

void SynthInstrument::SetParamValue(int idx, int v) {
  if (idx < 0 || idx >= kParamCount) {
    return;
  }
  if (idx == PARAM_NAME) {
    // Name is stored in name_, not in the packed array.
    return;
  }
  if (v < (int)SPECS[idx].min) {
    v = SPECS[idx].min;
  } else if (v > (int)SPECS[idx].max) {
    v = SPECS[idx].max;
  }
  if (params_[idx] == v) {
    return;
  }
  params_[idx] = v;
  SetChanged();
  NotifyObservers();
}

bool SynthInstrument::IsParamModified(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return false;
  }
  return params_[idx] != (int)SPECS[idx].default_;
}

void SynthInstrument::ResetParam(int idx) {
  if (idx < 0 || idx >= kParamCount || idx == PARAM_NAME) {
    return;
  }
  params_[idx] = SPECS[idx].default_;
}

void SynthInstrument::ResetAllParams() {
  for (int i = 1; i < kParamCount; i++) { // skip name slot at idx 0
    params_[i] = SPECS[i].default_;
  }
  params_[PARAM_TABLE] = -1; // restore legacy default
}
