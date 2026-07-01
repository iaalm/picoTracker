/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Player/TablePlayback.h"
#include "I_Instrument.h"
#include "ParamSpec.h"
#include "SynthVoice.h"
#include <cstdint>

// Display name lists shared by the UI.
extern const char *synthWaveNames[SYNTH_WAVE_LAST];
extern const char *synthLFOShapeNames[SYNTH_LFO_LAST];
extern const char *synthLFOTargetNames[SYNTH_LFO_TGT_LAST];
extern const char *synthFilterModeNames[SYNTH_FLT_LAST];
extern const char *synthAlgorithmNames[5];

class SynthInstrument : public I_Instrument {
public:
  // 37 packed UI parameters + 1 reserved name slot. Index 0 is the
  // instrument name in this uniform param table; the actual string lives in
  // the base-class `name_` member. Indices 1..37 are the synth parameters
  // in the same order as the legacy Variables() list.
  static constexpr int kParamCount = 38;
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[kParamCount];
  static const char *const FORMATS[kParamCount];

  // Parameter indices into params_[]. Used by fillSynthParameters and the
  // runtime hooks (Start / buildParams) to read/write the packed array.
  enum ParamIdx {
    PARAM_NAME = 0,
    // routing
    PARAM_ALGORITHM = 1,
    PARAM_FEEDBACK = 2,
    PARAM_FEEDBACK_OP = 3,
    // op1
    PARAM_OP1_WAVE = 4,
    PARAM_OP1_PW = 5,
    PARAM_OP1_LEVEL = 6,
    PARAM_OP1_ADSR = 7,
    // op2
    PARAM_OP2_WAVE = 8,
    PARAM_OP2_RATIO = 9,
    PARAM_OP2_DETUNE = 10,
    PARAM_OP2_LEVEL = 11,
    PARAM_OP2_ADSR = 12,
    // op3
    PARAM_OP3_WAVE = 13,
    PARAM_OP3_RATIO = 14,
    PARAM_OP3_DETUNE = 15,
    PARAM_OP3_LEVEL = 16,
    PARAM_OP3_ADSR = 17,
    // filter
    PARAM_FLT_CUTOFF = 18,
    PARAM_FLT_RESO = 19,
    PARAM_FLT_MODE = 20,
    PARAM_FLT_KEYTRACK = 21,
    PARAM_FLT_ENV_DEPTH = 22,
    PARAM_FLT_ADSR = 23,
    // pitch env
    PARAM_PITCH_DEPTH = 24,
    PARAM_PITCH_AD = 25,
    // lfo
    PARAM_LFO_RATE = 26,
    PARAM_LFO_SHAPE = 27,
    PARAM_LFO_DEPTH = 28,
    PARAM_LFO_TARGET = 29,
    PARAM_LFO_DELAY = 30,
    // misc
    PARAM_PORTAMENTO = 31,
    PARAM_HARD_SYNC = 32,
    PARAM_RING_MOD = 33,
    PARAM_SUB_LEVEL = 34,
    PARAM_VOLUME = 35,
    PARAM_TABLE = 36,
    PARAM_TABLE_AUTO = 37,
  };

  SynthInstrument();
  virtual ~SynthInstrument();

  virtual bool Init();
  virtual bool Start(int channel, unsigned char note, bool retrigger = true);
  virtual void Stop(int channel);
  virtual void OnStart();
  virtual bool Render(int channel, fixed *buffer, int size, bool updateTick);
  virtual void ProcessCommand(int channel, FourCC cc, ushort value);

  virtual bool IsInitialized();
  virtual bool IsEmpty() { return false; };
  virtual InstrumentType GetType() { return IT_SYNTH; };

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);

  // Stage 3: returns nullptr — Synth stores its parameters in the packed
  // array, not in Variables().
  const etl::ivector<Variable *> *Variables() const override { return nullptr; }

  // --- Plan B new parameter API (stage 3: directly on packed array) ---
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

private:
  // Build a VoiceParams snapshot from the current packed-array values.
  void buildParams(VoiceParams &p);

  // Packed parameter storage. Replaces 37 Variable members + the
  // etl::vector<Variable*, 40>; per-instance RAM drops from ~1250 B to
  // ≈ 320 B (38 * 4 = 152 B params + base-class overhead).
  int32_t params_[kParamCount];
  static_assert(sizeof(params_) == kParamCount * 4,
                "params_ must be tightly packed");

  TableSaveState tableState_;

  // One voice per channel, shared across all SynthInstrument instances.
  // Indexed by channel, never by instrument, so different instruments writing
  // to different channels can never collide.
  static SynthVoice voices_[SONG_CHANNEL_COUNT];
};

// Sized so the etl::pool<SynthInstrument, 8> allocation is bounded; bump this
// ceiling if a future patch legitimately grows the instrument. The packed
// 38-slot params_ array is just 152 B; the rest is the I_Instrument /
// VariableContainer / Observable / Persistent base classes. Pre-migration
// shape was ~1250 B (37 Variables × 32 B + 40-vec).
static_assert(sizeof(SynthInstrument) <= 384,
              "SynthInstrument exceeds stage-3 budget — re-measure params_/"
              "base classes for unexpected growth");

#endif
