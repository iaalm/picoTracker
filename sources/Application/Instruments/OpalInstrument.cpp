/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "OpalInstrument.h"
#include "CommandList.h"
#include "Externals/etl/include/etl/to_string.h"
#include "I_Instrument.h"
#include "System/Console/Trace.h"
#include "System/Profiler/Profiler.h"
#include "bit.h"
#include <string.h>

static const char *algorithms[2] = {"1*2", "1+2"};

static const char *waveShapes[8] = {"sine", "half", "abs", "puls",
                                    "even", "ab-e", "sqr", "dsqr"};

static const char *kslValues[4] = {"0", "1.5", "3", "6"};

#define FREQ_BASE_REG 0xA0
#define OCTAVE_BASE_REG 0xB0

#define CHANNEL 0 // just hardcoding to channel 0 for now

static const unsigned int noteFNumbers[] = {342, 363, 385, 408, 432, 458,
                                            485, 514, 544, 577, 611, 647};

// ---------------------------------------------------------------------------
// Plan B: static ParamSpec / NAMES / FORMATS tables for OpalInstrument.
//
// Each ParamSpec takes 16 B in flash. Persistence formats are plain decimal
// for round-trip compatibility with existing .pti files. UI display
// formatting ("%2.2X", "%s" for waveform/algorithm lists) is supplied by
// the UIParamIntVarField's own format string at the call site.
// ---------------------------------------------------------------------------

// ParamSpec positional layout (matches ParamSpec.h):
//   id (FourCC), _pad0 (u8), name_off (u16), format_off (u16),
//   default_ (i32), min (u16), max (u16), step (u8), big_step (u8),
//   _pad1 (u8), _pad2 (u8)
const ParamSpec OpalInstrument::SPECS[OpalInstrument::kParamCount] = {
    // [0] reserved name slot
    {FourCC::InstrumentName, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
    // [1] Algorithm (CHAR_LIST)
    {FourCC::OPALInstrumentAlgorithm, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    // [2] Feedback
    {FourCC::OPALInstrumentFeedback, 0, 2, 1, 0, 0, 0x07, 1, 1, 0, 0},
    // [3] DeepTremVib (2 bits)
    {FourCC::OPALInstrumentDeepTremeloVibrato, 0, 3, 1, 0, 0, 0x03, 1, 1, 0, 0},
    // [4] Op1Level
    {FourCC::OPALInstrumentOp1Level, 0, 4, 1, 0x17, 0, 63, 1, 1, 0, 0},
    // [5] Op1Multiplier
    {FourCC::OPALInstrumentOp1Multiplier, 0, 5, 1, 0x1, 0, 15, 1, 1, 0, 0},
    // [6] Op1ADSR
    {FourCC::OPALInstrumentOp1ADSR, 0, 6, 1, 0xF1C8, 0, 0xFFFF, 1, 1, 0, 0},
    // [7] Op1WaveShape (CHAR_LIST)
    {FourCC::OPALInstrumentOp1WaveShape, 0, 7, 1, 0, 0, 7, 1, 1, 0, 0},
    // [8] Op1KeyScaleLevel (CHAR_LIST)
    {FourCC::OPALInstrumentOp1KeyScaleLevel, 0, 8, 1, 0x1, 0, 3, 1, 1, 0, 0},
    // [9] Op1TremVibSusKSR (4 bits)
    {FourCC::OPALInstrumentOp1TremVibSusKSR, 0, 9, 1, 0, 0, 0x0F, 1, 1, 0, 0},
    // [10] Op2Level
    {FourCC::OPALInstrumentOp2Level, 0, 10, 1, 0, 0, 63, 1, 1, 0, 0},
    // [11] Op2Multiplier
    {FourCC::OPALInstrumentOp2Multiplier, 0, 11, 1, 0x1, 0, 15, 1, 1, 0, 0},
    // [12] Op2ADSR
    {FourCC::OPALInstrumentOp2ADSR, 0, 12, 1, 0xF1D8, 0, 0xFFFF, 1, 1, 0, 0},
    // [13] Op2WaveShape (CHAR_LIST)
    {FourCC::OPALInstrumentOp2WaveShape, 0, 13, 1, 0, 0, 7, 1, 1, 0, 0},
    // [14] Op2KeyScaleLevel (CHAR_LIST)
    {FourCC::OPALInstrumentOp2KeyScaleLevel, 0, 14, 1, 0, 0, 3, 1, 1, 0, 0},
    // [15] Op2TremVibSusKSR (4 bits)
    {FourCC::OPALInstrumentOp2TremVibSusKSR, 0, 15, 1, 0x2, 0, 0x0F, 1, 1, 0, 0},
};

const char *const OpalInstrument::NAMES[OpalInstrument::kParamCount] = {
    /*  0 */ "InstrumentName",
    /*  1 */ "ALGORITHM",
    /*  2 */ "FEEDBACK",
    /*  3 */ "DEEPTREMELOVIBRATO",
    /*  4 */ "OP1LEVEL",
    /*  5 */ "OP1MULTIPLIER",
    /*  6 */ "OP1ADSR",
    /*  7 */ "OP1WAVESHAPE",
    /*  8 */ "OP1KEYSCALELEVEL",
    /*  9 */ "OP1TREMVIBSUSKSR",
    /* 10 */ "OP2LEVEL",
    /* 11 */ "OP2MULTIPLIER",
    /* 12 */ "OP2ADSR",
    /* 13 */ "OP2WAVESHAPE",
    /* 14 */ "OP2KEYSCALELEVEL",
    /* 15 */ "OP2TREMVIBSUSKSR",
};

const char *const OpalInstrument::FORMATS[OpalInstrument::kParamCount] = {
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
    "%d", "%d", "%d", "%d", "%d", "%d", "%d",
};

OpalInstrument::OpalInstrument() : I_Instrument(nullptr), breg(0) {
  // Initialise the packed array to each ParamSpec's default. Mirrors the
  // legacy Variable member initialisers in the pre-stage-2 constructor.
  for (int i = 0; i < kParamCount; i++) {
    params_[i] = SPECS[i].default_;
  }
}

OpalInstrument::~OpalInstrument(){};

bool OpalInstrument::Init() {
  // enable left/right only for 0 channel
  opl_.Port(0xC0 + CHANNEL, 0x30);

  return true;
};

void OpalInstrument::OnStart(){};

bool OpalInstrument::Start(int channel, unsigned char note, bool retrigger) {
  int algorithm = params_[PARAM_ALGORITHM];

  // set note in OPAL
  uint8_t block = note / 12;
  uint16_t fnum = noteFNumbers[note % 12];

  // multiplier is only 4bits
  uint8_t freqMultOp1 = (params_[PARAM_OP1_MULTIPLIER] & 0xF);
  uint8_t freqMultOp2 = (params_[PARAM_OP2_MULTIPLIER] & 0xF);

  uint8_t tremVibSusKSR1 = params_[PARAM_OP1_TREM_VIB_SUS_KSR];
  uint8_t tremVibSusKSR2 = params_[PARAM_OP2_TREM_VIB_SUS_KSR];

  // channel wide settings
  // enable left/right output (D4, D5) & set algorithm D0
  // for now only 2 op so just Additive or FM
  opl_.Port(0xC0 + CHANNEL, 0x30 + algorithm);

  uint8_t tvskmOp1 = (tremVibSusKSR1 << 4) + freqMultOp1;
  uint8_t tvskmOp2 = (tremVibSusKSR2 << 4) + freqMultOp2;

  // For proper monophonic behavior and to support slides:
  // 1. First update the frequency registers without changing key-on bit
  // 2. Only retrigger the note (key-off then key-on) if retrigger is true

  // Set the frequency (low 8 bits)
  uint8_t areg = fnum & 0xFF;
  opl_.Port(FREQ_BASE_REG + CHANNEL, areg);

  // Prepare the block/high-freq bits with key-on bit
  uint8_t new_breg = 0x20 | (block << 2) | (fnum >> 8);

  if (retrigger) {
    // For retriggering, we need to key-off first to restart the envelope
    uint8_t key_off = BitClr(new_breg, 5); // Clear key-on bit from new value
    opl_.Port(OCTAVE_BASE_REG + CHANNEL, key_off);
  }

  // Store the new register value for future reference
  breg = new_breg;

  // Note on, block, hi freq - this will set the key-on bit
  opl_.Port(OCTAVE_BASE_REG + CHANNEL, breg);
  // Tremolo/Vibrato/Sustain/KSR/Multiplication
  opl_.Port(0x20 + CHANNEL, tvskmOp1);
  opl_.Port(0x21 + CHANNEL, tvskmOp2);

  // 0 = pure sine
  uint8_t waveform1 = params_[PARAM_OP1_WAVESHAPE];
  uint8_t waveform2 = params_[PARAM_OP2_WAVESHAPE];

  uint8_t keyscale = (params_[PARAM_OP1_KEYSCALE] & 0x03);
  // level is bottom 6 bits, keyscale top 2 bits
  uint8_t keyscaleOutLvl1 = (keyscale << 6) + (params_[PARAM_OP1_LEVEL] & 0x3F);
  uint8_t keyscaleOutLvl2 = (keyscale << 6) + (params_[PARAM_OP2_LEVEL] & 0x3F);

  uint16_t adsr1 = params_[PARAM_OP1_ADSR];
  uint16_t adsr2 = params_[PARAM_OP2_ADSR];

  // Waveform
  opl_.Port(0xE0 + CHANNEL, waveform1);
  opl_.Port(0xE1 + CHANNEL, waveform2);

  // Key Scale Level/Output Level
  opl_.Port(0x40 + CHANNEL, keyscaleOutLvl1);
  opl_.Port(0x41 + CHANNEL, keyscaleOutLvl2);

  // Attack Rate/Decay Rate
  opl_.Port(0x60 + CHANNEL, adsr1 >> 8);
  opl_.Port(0x61 + CHANNEL, adsr2 >> 8);

  // Sustain Level/Release Rate
  opl_.Port(0x80 + CHANNEL, (uint8_t)(adsr1 & 0x00FF));
  opl_.Port(0x81 + CHANNEL, (uint8_t)(adsr2 & 0x00FF));

  return true;
};

void OpalInstrument::Stop(int c) {
  uint8_t stop = BitClr(breg, 5);
  opl_.Port(OCTAVE_BASE_REG, stop);
};

bool OpalInstrument::Render(int channel, fixed *buffer, int size,
                            bool updateTick) {
  PROFILE_SCOPE("OpalInstrument::Render");

  // optimise to remove function calls in hot loop
  opl_.SampleBuffer(buffer, size);

  return true;
};

bool OpalInstrument::IsInitialized() {
  return true; // Always initialised
};

void OpalInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
  switch (cc) {
  case FourCC::InstrumentCommandGateOff:
    uint8_t stop = BitClr(breg, 5);
    opl_.Port(OCTAVE_BASE_REG, stop);
    break;
  }
};

int OpalInstrument::GetTable() {
  //  Variable *v = FindVariable(MIP_TABLE);
  //  return v->GetInt();
  return 0;
};

bool OpalInstrument::GetTableAutomation() {
  //  Variable *v = FindVariable(MIP_TABLEAUTO);
  //  return v->GetBool();
  return 0;
};

void OpalInstrument::GetTableState(TableSaveState &state){

};

void OpalInstrument::SetTableState(TableSaveState &state){};

// ---------------------------------------------------------------------------
// Plan B: new parameter API overrides for the packed array.
// ---------------------------------------------------------------------------

FourCC OpalInstrument::GetParamID(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return FourCC::Default;
  }
  return SPECS[idx].id;
}

const char *OpalInstrument::GetParamName(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "";
  }
  return NAMES[idx];
}

const char *OpalInstrument::GetParamFormat(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "%d";
  }
  return FORMATS[idx];
}

int OpalInstrument::GetParamMin(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return SPECS[idx].min;
}

int OpalInstrument::GetParamMax(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0xFFFF;
  }
  return SPECS[idx].max;
}

int OpalInstrument::GetParamDefault(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return (int)SPECS[idx].default_;
}

int OpalInstrument::GetParamStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].step;
}

int OpalInstrument::GetParamBigStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].big_step;
}

void OpalInstrument::SetParamValue(int idx, int v) {
  if (idx < 0 || idx >= kParamCount) {
    return;
  }
  if (idx == PARAM_NAME) {
    // Name is stored in name_, not in the packed array — callers should
    // use SetName(...) for the name slot.
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

bool OpalInstrument::IsParamModified(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return false;
  }
  return params_[idx] != (int)SPECS[idx].default_;
}

void OpalInstrument::ResetParam(int idx) {
  if (idx < 0 || idx >= kParamCount || idx == PARAM_NAME) {
    return;
  }
  params_[idx] = SPECS[idx].default_;
}

void OpalInstrument::ResetAllParams() {
  for (int i = 1; i < kParamCount; i++) { // skip name slot at idx 0
    params_[i] = SPECS[i].default_;
  }
}
