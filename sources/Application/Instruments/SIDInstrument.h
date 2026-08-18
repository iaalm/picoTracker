/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _CRSID_INSTRUMENT_H_
#define _CRSID_INSTRUMENT_H_

#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/cRSID/SID.h"
#include "I_Instrument.h"
#include "ParamSpec.h"

enum SIDInstrumentInstance { SID1 = 1, SID2 };

enum SIDInstrumentWaveform {
  DWF_NONE = 0,
  DWF_TRI,
  DWF_SAW,
  DWF_TRI_SAW,
  DWF_SQ,
  DWF_TRI_SQ,
  DWF_SAW_SQ,
  DWF_TRI_SAW_SQ,
  DWF_NOISE,
  DWF_LAST
};
// TODO: filter modes are additive, so they can be used together. Does it make
// sense to offer all combinations, or just some that make sense?
enum SIDInstrumentFilterMode {
  DFM_LP = 0,
  DFM_BP,
  DFM_HP,
  DFM_NOTCH,
  DFM_LAST
};

static const unsigned short sid_notes[96] = {
    0x0112, 0x0123, 0x0134, 0x0146, 0x015A, 0x016E, 0x0184, 0x018B, 0x01B3,
    0x01CD, 0x01E9, 0x0206, 0x0225, 0x0245, 0x0268, 0x028C, 0x02B3, 0x02DC,
    0x0308, 0x0336, 0x0367, 0x039B, 0x03D2, 0x040C, 0x0449, 0x048B, 0x04D0,
    0x0519, 0x0567, 0x05B9, 0x0610, 0x066C, 0x06CE, 0x0735, 0x07A3, 0x0817,
    0x0893, 0x0915, 0x099F, 0x0A32, 0x0ACD, 0x0B72, 0x0C20, 0x0CD8, 0x0D9C,
    0x0E6B, 0x0F46, 0x102F, 0x1125, 0x122A, 0x133F, 0x1464, 0x159A, 0x16E3,
    0x183F, 0x1981, 0x1B38, 0x1CD6, 0x1E80, 0x205E, 0x224B, 0x2455, 0x267E,
    0x28C8, 0x2B34, 0x2DC6, 0x2DC6, 0x3361, 0x366F, 0x39AC, 0x3D1A, 0x40BC,
    0x4495, 0x48A9, 0x4CFC, 0x518F, 0x5669, 0x5B8C, 0x60FE, 0x6602, 0x6CDF,
    0x7358, 0x7A34, 0x8178, 0x892B, 0x9153, 0x99F7, 0xA31F, 0xACD2, 0xB719,
    0xC1FC, 0xCD85, 0xD9BD, 0xE6B0, 0xF467, 0xFFFF}; // last one is 0x1F2F0

class SIDInstrument : public I_Instrument {

public:
  // 17 packed UI parameters + 1 reserved name slot. Index 0 is the
  // instrument name in this uniform param table; the actual string lives in
  // the base-class `name_` member. Indices 1..17 are the SID-specific
  // parameters in fixed order, mirroring the previous Variables() list.
  static constexpr int kParamCount = 18;
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[kParamCount];
  static const char *const FORMATS[kParamCount];

  // Parameter indices into params_[]. Used by fillSIDParameters and the
  // runtime hooks (Start / ProcessCommand) to read/write the packed array.
  enum ParamIdx {
    PARAM_NAME = 0,
    PARAM_VPW = 1,           // PulseWidth
    PARAM_VWF = 2,           // Waveform
    PARAM_VSYNC = 3,         // VSync (bool)
    PARAM_VRING = 4,         // RingMod (bool)
    PARAM_VADSR = 5,         // ADSR
    PARAM_VFON = 6,          // FilterOn (bool)
    PARAM_TABLE = 7,         // Table
    PARAM_TABLE_AUTO = 8,    // TableAutomation (bool)
    PARAM_OSC = 9,           // OSCNumber
    PARAM_FLTCUT_1 = 10,     // Filter Cut (SID1)
    PARAM_FLTRES_1 = 11,     // Filter Resonance (SID1)
    PARAM_FLTMODE_1 = 12,    // Filter Mode (SID1)
    PARAM_VOL_1 = 13,        // Volume (SID1)
    PARAM_FLTCUT_2 = 14,     // Filter Cut (SID2)
    PARAM_FLTRES_2 = 15,     // Filter Resonance (SID2)
    PARAM_FLTMODE_2 = 16,    // Filter Mode (SID2)
    PARAM_VOL_2 = 17,        // Volume (SID2)
  };

  SIDInstrument(SIDInstrumentInstance chip);
  virtual ~SIDInstrument();

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

  virtual InstrumentType GetType() { return IT_SID; };

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetName();

  virtual void OnStart();

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);

  // Stage 1: returns nullptr — SID stores its parameters in the packed
  // array, not in Variables(). The legacy `Variables()` interface is no
  // longer the source of truth for this instrument.
  const etl::ivector<Variable *> *Variables() const override { return nullptr; }

  // --- Plan B new parameter API (stage 1: directly on packed array) ---
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

  // waveform was CHAR_LIST and vsync/ring/filter-on/table-automation were
  // BOOLs pre-migration; legacy .pti files store them as words.
  const StringParam *StringParams(int &count) const override;

  SIDInstrumentInstance GetChip() { return chip_; };
  unsigned short GetOsc() { return (unsigned short)params_[PARAM_OSC]; }
  void SetRender(bool render) { render_ = render; };

  // returns just the chip name, eg "SID #1"
  const char *GetChipName() { return (chip_ == SID1) ? "SID #1" : "SID #2"; };

private:
  // Packed parameter storage. Replaces the 17 Variable members + the legacy
  // static fltcut1/fltcut2/etc. Variables; per-instance RAM drops from ~400 B
  // to ~96 B (18 * 4 = 72 B params + chip + chip-bound indices + flags +
  // table state).
  int32_t params_[kParamCount];
  static_assert(sizeof(params_) == kParamCount * 4,
                "params_ must be tightly packed");

  SIDInstrumentInstance chip_; // SID1 or SID2

  // Index helpers bound in Init() to the active chip's parameter slot.
  int fltcutIdx_;
  int fltresIdx_;
  int fltmodeIdx_;
  int volIdx_;

  bool render_ = false;

  bool playing_;
  bool gate_;
  TableSaveState tableState_;
  static cRSID sid1_;
  static cRSID sid2_;
  cRSID *sid_;
  static SIDInstrument *SID1RenderMaster;
  static SIDInstrument *SID2RenderMaster;
};

// Documented per-instance RAM budget after stage 1 migration. The packed
// array is 72 B; the rest are flags, the chip-bound index slots, the
// I_Instrument / VariableContainer / Observable / Persistent base-class
// vtable + members, and TableSaveState. This drops from the pre-migration
// ~700 B (17 Variables × 32 B + shared-statics pointer + per-instance
// overhead) once the legacy Variables and per-chip shared Variables are
// gone. Sized so the etl::pool<SIDInstrument, 3> allocation is bounded;
// bump this ceiling if a future patch legitimately grows the instrument.
#ifndef HOST_TEST
static_assert(sizeof(SIDInstrument) <= 256,
              "SIDInstrument exceeds stage-1 budget — re-measure "
              "params_/indices/flags for unexpected growth");
#endif

#endif
