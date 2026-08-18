/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _OPAL_INSTRUMENT_H_
#define _OPAL_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/opal/opal.h"
#include "I_Instrument.h"
#include "ParamSpec.h"
#include <cstdint>

#define OPAL_MAX_CHANNELS 4

class OpalInstrument : public I_Instrument {

public:
  // 15 packed UI parameters + 1 reserved name slot. Index 0 is the
  // instrument name in this uniform param table; the actual string lives in
  // the base-class `name_` member. Indices 1..15 are the OPAL parameters in
  // the same order as the legacy Variables() list.
  static constexpr int kParamCount = 16;
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[kParamCount];
  static const char *const FORMATS[kParamCount];

  // Parameter indices into params_[]. Used by fillOpalParameters and the
  // runtime hooks (Start / ProcessCommand) to read/write the packed array.
  enum ParamIdx {
    PARAM_NAME = 0,
    PARAM_ALGORITHM = 1,
    PARAM_FEEDBACK = 2,
    PARAM_DEEP_TREM_VIB = 3,
    PARAM_OP1_LEVEL = 4,
    PARAM_OP1_MULTIPLIER = 5,
    PARAM_OP1_ADSR = 6,
    PARAM_OP1_WAVESHAPE = 7,
    PARAM_OP1_KEYSCALE = 8,
    PARAM_OP1_TREM_VIB_SUS_KSR = 9,
    PARAM_OP2_LEVEL = 10,
    PARAM_OP2_MULTIPLIER = 11,
    PARAM_OP2_ADSR = 12,
    PARAM_OP2_WAVESHAPE = 13,
    PARAM_OP2_KEYSCALE = 14,
    PARAM_OP2_TREM_VIB_SUS_KSR = 15,
  };

  OpalInstrument();
  virtual ~OpalInstrument();

  virtual bool Init();

  // Start & stop the instument
  virtual bool Start(int channel, unsigned char note, bool retrigger = true);
  virtual void Stop(int channel);

  // size refers to the number of samples
  // should always fill interleaved stereo / 16bit
  // return value is true if any audio was rendered
  virtual bool Render(int channel, fixed *buffer, int size, bool updateTick);
  virtual void ProcessCommand(int channel, FourCC cc, ushort value);

  virtual bool IsInitialized();

  virtual bool IsEmpty() { return false; };

  virtual InstrumentType GetType() { return IT_OPAL; };

  virtual void OnStart();

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);

  // Stage 2: returns nullptr — OPAL stores its parameters in the packed
  // array, not in Variables().
  const etl::ivector<Variable *> *Variables() const override { return nullptr; }

  // --- Plan B new parameter API (stage 2: directly on packed array) ---
  virtual int GetParamCount() const override { return kParamCount; }
  virtual FourCC GetParamID(int idx) const override;
  virtual const char *GetParamName(int idx) const override;
  virtual const char *GetParamFormat(int idx) const override;
  virtual int GetParamMin(int idx) const override;
  virtual int GetParamMax(int idx) const override;
  virtual int GetParamDefault(int idx) const override;
  virtual int GetParamStep(int idx) const override;
  virtual int GetParamBigStep(int idx) const override;
  virtual int GetParamValue(int idx) const override { return params_[idx]; }
  virtual void SetParamValue(int idx, int v) override;
  virtual bool IsParamModified(int idx) const override;
  virtual void ResetParam(int idx) override;
  virtual void ResetAllParams() override;

  // algorithm / waveshapes / key-scale levels were CHAR_LIST Variables
  // pre-migration; legacy .pti files store them as words.
  const StringParam *StringParams(int &count) const override;

  void setChannel(uint8_t channel);

private:
  Opal opl_ = (44100);

  uint8_t breg;

  // Packed parameter storage. Replaces 15 Variable members + the
  // etl::vector<Variable*, 16>; per-instance RAM drops from ~700 B to
  // ≈ 200 B (16 * 4 = 64 B params + Opal DSP embed + flag + register cache
  // + base-class overhead).
  int32_t params_[kParamCount];
  static_assert(sizeof(params_) == kParamCount * 4,
                "params_ must be tightly packed");
};

// Sized so the etl::pool<OpalInstrument, 3> allocation is bounded; bump this
// ceiling if a future patch legitimately grows the instrument. The dominant
// fixed cost is the embedded `Opal` DSP state (an OPL3 emulator) — the
// packed 16-slot params_ array is just 64 B; the rest is the DSP, the
// I_Instrument / VariableContainer / Observable / Persistent base classes,
// and a 1-byte register cache. Pre-migration shape was ~700 B
// (15 Variables × 32 B + 16-vec).
#ifndef HOST_TEST
static_assert(sizeof(OpalInstrument) <= 416,
              "OpalInstrument exceeds stage-2 budget — re-measure params_/"
              "Opal DSP embed for unexpected growth");
#endif

#endif
