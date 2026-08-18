/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SAMPLE_INSTRUMENT_H_
#define _SAMPLE_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/array.h"
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "I_Instrument.h"
#include "ParamSpec.h"
#include "SRPUpdaters.h"
#include "SampleRenderingParams.h"
#include "SampleVariable.h"
#include "SoundSource.h"

enum SampleInstrumentLoopMode {
  SILM_ONESHOT = 0,
  SILM_LOOP,
  SILM_LOOP_PINGPONG,
  SILM_OSC,
  //	SILM_OSCFINE,
  SILM_LOOPSYNC,
  SILM_LAST
};

#define NO_SAMPLE (-1)

class SampleInstrument : public I_Instrument, I_Observer {

public:
  // 18 packed UI parameters + 1 reserved name slot. Index 0 is the
  // instrument name in this uniform param table; the actual string lives in
  // the base-class `name_` member. Indices 1..18 are the sample parameters
  // in the same order as the legacy Variables() list (excluding the
  // SampleInstrumentSample field, which stays as a legacy Variable per
  // docs/§9.4).
  static constexpr int kParamCount = 19;
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[kParamCount];
  static const char *const FORMATS[kParamCount];

  // Parameter indices into params_[]. Used by fillSampleParameters and the
  // runtime hooks (Start / RestoreContent) to read/write the packed array.
  enum ParamIdx {
    PARAM_NAME = 0,
    PARAM_VOLUME = 1,
    PARAM_INTERPOLATION = 2,
    PARAM_CRUSH = 3,
    PARAM_DRIVE = 4,
    PARAM_DOWNSAMPLE = 5,
    PARAM_ROOT_NOTE = 6,
    PARAM_FINE_TUNE = 7,
    PARAM_PAN = 8,
    PARAM_CUTOFF = 9,
    PARAM_RESO = 10,
    PARAM_FILTER_MIX = 11,
    PARAM_FILTER_MODE = 12,
    PARAM_START = 13,
    PARAM_LOOP_MODE = 14,
    PARAM_LOOP_START = 15,
    PARAM_LOOP_END = 16,
    PARAM_TABLE = 17,
    PARAM_TABLE_AUTO = 18,
  };

  SampleInstrument();
  virtual ~SampleInstrument();
  // I_Instrument implementation
  virtual bool Init();
  virtual bool Start(int channel, unsigned char note, bool trigger = true);
  virtual void Stop(int channel);
  virtual bool Render(int channel, fixed *buffer, int size, bool updateTick);
  virtual bool IsInitialized();
  virtual bool IsEmpty();

  virtual InstrumentType GetType() { return IT_SAMPLE; };
  virtual void ProcessCommand(int channel, FourCC cc, ushort value);
  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);

  // Returns nullptr like every other migrated instrument. `sample_` is still
  // a legacy Variable and stays reachable via FindVariable() — which reads
  // VariableContainer::list_, set from variables_ in the constructor — but it
  // must NOT be exposed here: I_Instrument::SaveContent indexes Variables()
  // with idx in [0, GetParamCount()) == [0, 19), and variables_ holds one
  // element, so returning it read 18 entries past the end of the buffer.
  const etl::ivector<Variable *> *Variables() const override { return nullptr; }

  bool IsMulti();

  // Engine playback  start callback

  virtual void OnStart();
  static constexpr size_t MaxSlices = 16;
  static constexpr unsigned char SliceNoteBase = 48;

  uint32_t GetSlicePoint(size_t index) const;
  void SetSlicePoint(size_t index, uint32_t start);
  void ClearSlices();
  bool HasSlicesForPlayback() const;
  bool HasSlicesForWarning() const;
  bool IsSliceDefined(size_t index) const;
  bool ShouldDisplaySliceForNote(uint8_t midinote) const;
  bool GetSliceNoteRange(uint8_t &first, uint8_t &last) const;

  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);
  // Additional
  void AssignSample(int i);
  int GetSampleIndex();
  int GetVolume();
  void SetVolume(int);
  int GetSampleSize(int channel = -1);
  float GetLengthInSec();

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetUserSetName();
  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDisplayName() override;
  virtual etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> GetSampleFileName();

  static void EnableDownsamplingLegacy();
  virtual void SaveContent(tinyxml2::XMLPrinter *printer) override;
  virtual void RestoreContent(PersistencyDocument *doc) override;
  void Purge();

  // --- Plan B new parameter API (stage 4: directly on packed array) ---
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

  // interpol / filter mode / loop mode were CHAR_LIST Variables, and table
  // automation a BOOL, before the packed-storage migration — legacy .pti
  // files store them as words ("linear", "original", "pingpong", "false").
  const StringParam *StringParams(int &count) const override;
  virtual bool IsParamModified(int idx) const override;
  virtual void ResetParam(int idx) override;
  virtual void ResetAllParams() override;

protected:
  void updateInstrumentData(bool search);
  void doTickUpdate(int channel);
  void doKRateUpdate(int channel);

private:
  // 1-element legacy Variables() vector — only SampleInstrumentSample lives
  // here. The 18 migrated parameters are not exposed via Variables() at all
  // (the field layer uses the new UIParam* classes for those).
  etl::vector<Variable *, 1> variables_;

  // Packed parameter storage. Replaces 18 Variable members; the legacy
  // SampleInstrumentSample (sample_) and slicePoints_ stay outside the
  // packed array.
  int32_t params_[kParamCount];
  static_assert(sizeof(params_) == kParamCount * 4,
                "params_ must be tightly packed");

  SoundSource *source_;
  __attribute__((section(".DTCMRAM"))) static struct renderParams
      renderParams_[SONG_CHANNEL_COUNT];
  bool running_;
  bool dirty_;
  TableSaveState tableState_;

  static signed char lastMidiNote_[SONG_CHANNEL_COUNT];
  static fixed lastSample_[SONG_CHANNEL_COUNT][2];

  // SampleInstrumentSample stays as a legacy Variable per docs/§9.4 — its
  // custom UI renderer (sample-name lookup via the SamplePool name list) and
  // custom persistence path are out of scope for the Plan B migration.
  SampleVariable sample_;

  // TODO (democloid): evaluate if this should be in DTCMRAM
  etl::array<uint32_t, MaxSlices> slicePoints_;

  static bool useDirtyDownsampling_;
  bool isSliceIndexActive(size_t index) const;
  bool shouldUseSlice(unsigned char midinote, size_t &sliceIndex,
                      uint32_t sampleSize) const;
  uint32_t computeSliceStart(size_t index, uint32_t sampleSize) const;
  uint32_t computeSliceEnd(size_t index, uint32_t sampleSize) const;
  bool hasAnySliceValue() const;
  void clampSlicePoints(uint32_t sampleSize);
};

// Sized so the etl::pool<SampleInstrument, 16> allocation is bounded; bump
// this ceiling if a future patch legitimately grows the instrument. The
// dominant fixed cost is the SampleVariable sample_ (a Variable with custom
// sample-name rendering) + the slicePoints_ array + the I_Instrument /
// VariableContainer / Observable / Persistent base classes. The packed
// 19-slot params_ array is just 76 B. Pre-migration shape was ~700 B
// (18 Variables × 32 B + 21-vec + 64 B slice points).
#ifndef HOST_TEST
static_assert(sizeof(SampleInstrument) <= 416,
              "SampleInstrument exceeds stage-4 budget — re-measure params_/"
              "sample_/slicePoints_ for unexpected growth");
#endif

#endif
