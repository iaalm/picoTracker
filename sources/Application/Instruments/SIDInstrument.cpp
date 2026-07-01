/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "SIDInstrument.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "CommandList.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "I_Instrument.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <string.h>

const char *sidWaveformText[DWF_LAST] = {
    "--------",
    char_waveform_tri_s "------",
    "--" char_waveform_saw_s "----",
    char_waveform_tri_s char_waveform_saw_s "----",
    "----" char_waveform_pulse_s "--",
    char_waveform_tri_s "--" char_waveform_pulse_s "--",
    "--" char_waveform_saw_s char_waveform_pulse_s "--",
    char_waveform_tri_s char_waveform_saw_s char_waveform_pulse_s "--",
    "------" char_waveform_noise_s};
const char *sidFilterModeText[DFM_LAST] = {"LP", "BP", "HP", "Notch"};

cRSID SIDInstrument::sid1_(44100);
SIDInstrument *SIDInstrument::SID1RenderMaster = 0;
cRSID SIDInstrument::sid2_(44100);
SIDInstrument *SIDInstrument::SID2RenderMaster = 0;

// ---------------------------------------------------------------------------
// Plan B: static ParamSpec / NAMES / FORMATS tables for SIDInstrument.
//
// Each ParamSpec takes 12 B in flash. The NAMES / FORMATS arrays are
// deduplicated by the (name_off, format_off) offset pair so repeated formats
// (e.g. "%d" for all decimals) share a single string entry.
// ---------------------------------------------------------------------------

// ParamSpec positional layout (matches ParamSpec.h):
//   id (FourCC), _pad0 (u8), name_off (u16), format_off (u16),
//   default_ (i32), min (u16), max (u16), step (u8), big_step (u8),
//   _pad1 (u8), _pad2 (u8)
const ParamSpec SIDInstrument::SPECS[SIDInstrument::kParamCount] = {
    // [0] reserved name slot
    {FourCC::InstrumentName, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
    // [1] PulseWidth
    {FourCC::SIDInstrumentPulseWidth, 0, 1, 1, 0x800, 0, 0xFFF, 1, 0x10, 0, 0},
    // [2] Waveform (CHAR_LIST)
    {FourCC::SIDInstrumentWaveform, 0, 2, 1, 0x1, 0, DWF_LAST - 1, 1, 1, 0, 0},
    // [3] VSync (bool)
    {FourCC::SIDInstrumentVSync, 0, 3, 1, 0, 0, 1, 1, 1, 0, 0},
    // [4] RingModulator (bool)
    {FourCC::SIDInstrumentRingModulator, 0, 4, 1, 0, 0, 1, 1, 1, 0, 0},
    // [5] ADSR
    {FourCC::SIDInstrumentADSR, 0, 5, 1, 0x2282, 0, 0xFFFF, 1, 0x10, 0, 0},
    // [6] FilterOn (bool)
    {FourCC::SIDInstrumentFilterOn, 0, 6, 1, 0, 0, 1, 1, 1, 0, 0},
    // [7] Table (default -1 == "no table bound")
    {FourCC::SIDInstrumentTable, 0, 7, 1, -1, 0, 0x7F, 1, 0x10, 0, 0},
    // [8] TableAutomation (bool)
    {FourCC::SIDInstrumentTableAutomation, 0, 8, 1, 0, 0, 1, 1, 1, 0, 0},
    // [9] OSCNumber
    {FourCC::SIDInstrumentOSCNumber, 0, 9, 1, 0, 0, 2, 1, 1, 0, 0},
    // [10..13] SID1 filter/volume
    {FourCC::SIDInstrument1FilterCut, 0, 10, 1, 0x1FF, 0, 0x7FF, 1, 0x10, 0, 0},
    {FourCC::SIDInstrument1FilterResonance, 0, 11, 1, 0, 0, 0xF, 1, 1, 0, 0},
    {FourCC::SIDInstrument1FilterMode, 0, 12, 1, 0, 0, DFM_LAST - 1, 1, 1, 0, 0},
    {FourCC::SIDInstrument1Volume, 0, 13, 1, 0xF, 0, 0xF, 1, 1, 0, 0},
    // [14..17] SID2 filter/volume
    {FourCC::SIDInstrument2FilterCut, 0, 14, 1, 0x1FF, 0, 0x7FF, 1, 0x10, 0, 0},
    {FourCC::SIDInstrument2FilterResonance, 0, 15, 1, 0, 0, 0xF, 1, 1, 0, 0},
    {FourCC::SIDInstrument2FilterMode, 0, 16, 1, 0, 0, DFM_LAST - 1, 1, 1, 0, 0},
    {FourCC::SIDInstrument2Volume, 0, 17, 1, 0xF, 0, 0xF, 1, 1, 0, 0},
};

const char *const SIDInstrument::NAMES[SIDInstrument::kParamCount] = {
    /*  0 */ "InstrumentName",
    /*  1 */ "SIDInstrumentPulseWidth",
    /*  2 */ "SIDInstrumentWaveform",
    /*  3 */ "SIDInstrumentVSync",
    /*  4 */ "SIDInstrumentRingModulator",
    /*  5 */ "SIDInstrumentADSR",
    /*  6 */ "SIDInstrumentFilterOn",
    /*  7 */ "SIDInstrumentTable",
    /*  8 */ "SIDInstrumentTableAutomation",
    /*  9 */ "SIDInstrumentOSCNumber",
    /* 10 */ "SIDInstrument1FilterCut",
    /* 11 */ "SIDInstrument1FilterResonance",
    /* 12 */ "SIDInstrument1FilterMode",
    /* 13 */ "SIDInstrument1Volume",
    /* 14 */ "SIDInstrument2FilterCut",
    /* 15 */ "SIDInstrument2FilterResonance",
    /* 16 */ "SIDInstrument2FilterMode",
    /* 17 */ "SIDInstrument2Volume",
};

// All persistence formats use plain decimal — keeps RestoreContent simple
// (atoi) and round-trips with existing .pti files where values were stored
// via Variable::GetString() ("%d"). Display-side formatting ("vpw: %4.4X")
// is supplied by the UIParamIntVarField's own format string at the call
// site, not via GetParamFormat.
const char *const SIDInstrument::FORMATS[SIDInstrument::kParamCount] = {
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
    "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d", "%d",
};

SIDInstrument::SIDInstrument(SIDInstrumentInstance chip)
    : I_Instrument(nullptr), chip_(chip), fltcutIdx_(0), fltresIdx_(0),
      fltmodeIdx_(0), volIdx_(0) {

  // Initialise the packed array to each ParamSpec's default. We iterate
  // SPECS[i].default_ in lieu of re-deriving from the Variable defaults.
  for (int i = 0; i < kParamCount; i++) {
    params_[i] = SPECS[i].default_;
  }
  // ParamSpec::default_ is unsigned; -1 wouldn't fit. Reapply the Table
  // "unbound" sentinel (-1) explicitly here, matching the legacy Variable
  // initialiser.
  params_[PARAM_TABLE] = -1;
}

SIDInstrument::~SIDInstrument(){};

bool SIDInstrument::Init() {
  tableState_.Reset();

  Trace::Debug("SID instrument chip is %i and osc is %i", chip_, GetOsc());
  switch (chip_) {
  case SID1:
    sid_ = &sid1_;
    fltcutIdx_ = PARAM_FLTCUT_1;
    fltresIdx_ = PARAM_FLTRES_1;
    fltmodeIdx_ = PARAM_FLTMODE_1;
    volIdx_ = PARAM_VOL_1;
    break;
  case SID2:
    sid_ = &sid2_;
    fltcutIdx_ = PARAM_FLTCUT_2;
    fltresIdx_ = PARAM_FLTRES_2;
    fltmodeIdx_ = PARAM_FLTMODE_2;
    volIdx_ = PARAM_VOL_2;
    break;
  default:
    return false;
  }

  return true;
};

void SIDInstrument::OnStart() {
  tableState_.Reset();
  int osc = GetOsc();
  sid_->cRSID_resetChannel(osc);
};

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)                                                   \
  ((byte) & 0x80 ? '1' : '0'), ((byte) & 0x40 ? '1' : '0'),                    \
      ((byte) & 0x20 ? '1' : '0'), ((byte) & 0x10 ? '1' : '0'),                \
      ((byte) & 0x08 ? '1' : '0'), ((byte) & 0x04 ? '1' : '0'),                \
      ((byte) & 0x02 ? '1' : '0'), ((byte) & 0x01 ? '1' : '0')

bool SIDInstrument::Start(int c, unsigned char note, bool retrigger) {
  Trace::Debug("Retrigger: %i", retrigger);
  gate_ = retrigger;
  // Select master render instrument
  // At each row of the sequencer we call start for each instrument in
  // the channel. With this we are ensuring that the only instrument that
  // renders audio is the last in use per SID chip (to ensure that all settings
  // are set before rendering and to only render once per chip)
  // I *think* that this could be done only on retrigger and would work fine
  switch (chip_) {
  case SID1:
    if (SID1RenderMaster) {
      SID1RenderMaster->SetRender(false);
      Trace::Debug("Previous renderer for SID1 was %s",
                   SID1RenderMaster->GetName().c_str());
    }
    SID1RenderMaster = this;
    SID1RenderMaster->SetRender(true);
    Trace::Debug("New renderer for SID1 is %s",
                 SID1RenderMaster->GetName().c_str());
    break;
  case SID2:
    if (SID2RenderMaster) {
      SID2RenderMaster->SetRender(false);
      Trace::Debug("Previous renderer for SID2 was %s",
                   SID2RenderMaster->GetName().c_str());
    }
    SID2RenderMaster = this;
    SID2RenderMaster->SetRender(true);
    Trace::Debug("New renderer for SID2 is %s",
                 SID2RenderMaster->GetName().c_str());
    break;
  }

  int osc = GetOsc();

  // On a true note trigger (an instrument number was present on the row) we
  // must restart the SID envelope. cRSID drives the ADSR purely from the gate
  // bit's edge, so without resetting the channel the internal "previous gate"
  // stays high across consecutive notes and the envelope never re-attacks
  // (the whole line plays with a single ADSR). Resetting the channel clears the
  // gate-tracking state so the upcoming gate=1 is seen as a rising edge.
  if (retrigger) {
    sid_->cRSID_resetChannel(osc);
  }

  int vpw = params_[PARAM_VPW];
  int vwf = params_[PARAM_VWF];
  int vring = params_[PARAM_VRING];
  int vsync = params_[PARAM_VSYNC];
  int vadsr = params_[PARAM_VADSR];
  int vfon = params_[PARAM_VFON];
  int fltcut = params_[fltcutIdx_];
  int fltres = params_[fltresIdx_];
  int fltmode = params_[fltmodeIdx_];
  int vol = params_[volIdx_];

  sid_->Register[0 + osc * 7] = sid_notes[note - 24] & 0xFF; // V1 Freq Lo
  sid_->Register[1 + osc * 7] = sid_notes[note - 24] >> 8;   // V1 Freq Hi
  sid_->Register[2 + osc * 7] = vpw & 0xFF;                 // V1 PW Lo
  sid_->Register[3 + osc * 7] = vpw >> 8;                   // V1 PW Hi
  sid_->Register[4 + osc * 7] = vwf << 4 | vring << 2 | vsync << 1 |
                                (int)gate_;             // V1 Control Reg
  sid_->Register[5 + osc * 7] = vadsr >> 8;             // V1 Attack/Decay
  sid_->Register[6 + osc * 7] = vadsr & 0xFF;           // V1 Sustain/Release

  // filter settings
  sid_->Register[21] = fltcut & 0x7; // Filter Cut lo
  sid_->Register[22] = fltcut >> 3;  // Filter Cut Hi

  // on start for each instrument it sets its own filter on bit in this register
  //  we need to clear filter resonance and the current oscillator's filter on
  //  bit while preserving the filter on bits of the other two oscillators for
  //  this chip
  sid_->Register[23] = (sid_->Register[23] & 0xF & ~(1 << osc)) |
                       fltres << 4 |        // filter resonance
                       vfon << osc;         // filter on bit for this osc

  int8_t mode = 0;
  switch (fltmode) {
  case DFM_LP:
    mode = 1;
    break;
  case DFM_BP:
    mode = 2;
    break;
  case DFM_HP:
    mode = 4;
    break;
  case DFM_NOTCH:
    mode = 5;
    break;
  }
  // TODO: implement v3off
  //  sid_->Register[24] =
  //      v3off_.GetInt() << 7 | mode << 4 | vol_.GetInt(); // Filter Mode/Vol

  sid_->Register[24] = 0 << 7 | mode << 4 | vol; // Filter Mode / Vol

  playing_ = true;

  return true;
};

void SIDInstrument::Stop(int c) {
  playing_ = false;
  int osc = GetOsc();
  sid_->Register[4 + osc * 7] &= ~1; // Set gate bit off
  gate_ = false;
};

bool SIDInstrument::Render(int channel, fixed *buffer, int size,
                           bool updateTick) {
  if (playing_ and render_) {

    // clear the fixed point buffer
    memset(buffer, 0, size * 2 * sizeof(fixed));

    sid_->cRSID_emulateWavesBuffer(buffer, size);

    return true;
  }
  return false;
};

bool SIDInstrument::IsInitialized() {
  return true; // Always initialised
};

void SIDInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
  switch (cc) {
  case FourCC::InstrumentCommandGateOff:
    int osc = GetOsc();
    sid_->Register[4 + osc * 7] &= ~1; // Set gate bit off
    gate_ = false;
    break;
  }
};

etl::string<MAX_INSTRUMENT_NAME_LENGTH> SIDInstrument::GetName() {
  // first check if the name_ string has been explicitly set
  if (!name_.empty()) {
    return name_;
  }
  // otherwise return the default name for this instrument type
  return etl::string<MAX_INSTRUMENT_NAME_LENGTH>(InstrumentTypeNames[IT_SID]);
}

int SIDInstrument::GetTable() { return params_[PARAM_TABLE]; };

bool SIDInstrument::GetTableAutomation() {
  return params_[PARAM_TABLE_AUTO] != 0;
};

void SIDInstrument::GetTableState(TableSaveState &state) {
  memcpy(state.hopCount_, tableState_.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
  state.groove_ = tableState_.groove_;
};

void SIDInstrument::SetTableState(TableSaveState &state) {
  memcpy(tableState_.hopCount_, state.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
  tableState_.groove_ = state.groove_;
};

// ---------------------------------------------------------------------------
// Plan B: new parameter API overrides for the packed array.
// ---------------------------------------------------------------------------

FourCC SIDInstrument::GetParamID(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return FourCC::Default;
  }
  return SPECS[idx].id;
}

const char *SIDInstrument::GetParamName(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "";
  }
  return NAMES[idx];
}

const char *SIDInstrument::GetParamFormat(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return "%d";
  }
  return FORMATS[idx];
}

int SIDInstrument::GetParamMin(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return SPECS[idx].min;
}

int SIDInstrument::GetParamMax(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0xFFFF;
  }
  return SPECS[idx].max;
}

int SIDInstrument::GetParamDefault(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 0;
  }
  return (int)SPECS[idx].default_;
}

int SIDInstrument::GetParamStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].step;
}

int SIDInstrument::GetParamBigStep(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return 1;
  }
  return SPECS[idx].big_step;
}

void SIDInstrument::SetParamValue(int idx, int v) {
  if (idx < 0 || idx >= kParamCount) {
    return;
  }
  if (idx == PARAM_NAME) {
    // Name is stored in name_, not in the packed array — callers should
    // use SetName(...) for the name slot. Keep the parameter API
    // symmetric, but ignore attempts to write the name as an int here.
    return;
  }
  // Clamp to [min, max] so direct API writes don't store out-of-range
  // values that the UI would also reject.
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

bool SIDInstrument::IsParamModified(int idx) const {
  if (idx < 0 || idx >= kParamCount) {
    return false;
  }
  return params_[idx] != (int)SPECS[idx].default_;
}

void SIDInstrument::ResetParam(int idx) {
  if (idx < 0 || idx >= kParamCount || idx == PARAM_NAME) {
    return;
  }
  params_[idx] = SPECS[idx].default_;
}

void SIDInstrument::ResetAllParams() {
  for (int i = 1; i < kParamCount; i++) { // skip name slot at idx 0
    params_[i] = SPECS[i].default_;
  }
  params_[PARAM_TABLE] = -1; // restore legacy default
}
