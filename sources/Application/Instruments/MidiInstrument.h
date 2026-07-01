/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _MIDI_INSTRUMENT_H_
#define _MIDI_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/string.h"
#include "I_Instrument.h"
#include "ParamSpec.h"
#include "Services/Midi/MidiMessage.h"
#include "Services/Midi/MidiService.h"

#define MAX_MIDI_CHORD_NOTES 4
#define INITIAL_NOTE_VELOCITY 0x7F

// Constants for MIDI pitch bend.
#define PB_CENTER 8192
#define PB_MAX 16383
#define PB_7BIT_MAX 127
// Pitch bend constants for exponential interpolation.
#define PB_MAX_GROWTH_FACTOR 2.0f
#define PB_MIN_GROWTH_FACTOR 1.0001f
#define PB_MAX_ALPHA 0.5f
#define PB_CURVE_SHAPE 2.0f

class MidiInstrument : public I_Instrument {

public:
  // 7 packed UI parameters + 1 reserved name slot. Index 0 is the
  // instrument name in this uniform param table; the actual string lives in
  // the base-class `name_` member. Indices 1..7 are the MIDI parameters in
  // the same order as the legacy Variables() list.
  static constexpr int kParamCount = 8;
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[kParamCount];
  static const char *const FORMATS[kParamCount];

  // Parameter indices into params_[]. Used by fillMidiParameters and the
  // runtime hooks (Start / OnStart / GetTable) to read/write the packed
  // array.
  enum ParamIdx {
    PARAM_NAME = 0,
    PARAM_CHANNEL = 1,
    PARAM_NOTE_LENGTH = 2,
    PARAM_VOLUME = 3,
    PARAM_TABLE = 4,
    PARAM_TABLE_AUTO = 5,
    PARAM_PROGRAM = 6,
    // Index 7 unused — kept to align with the legacy 7-slot Variables()
    // count so the param array's kParamCount matches the 8 reserved slots.
    PARAM_UNUSED_7 = 7,
  };

  MidiInstrument();
  virtual ~MidiInstrument();

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

  virtual InstrumentType GetType() { return IT_MIDI; };

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDefaultName();

  virtual void OnStart();

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);

  // Stage 5: returns nullptr — MIDI stores its parameters in the packed
  // array, not in Variables().
  const etl::ivector<Variable *> *Variables() const override { return nullptr; }

  // --- Plan B new parameter API (stage 5: directly on packed array) ---
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

  void SetChannel(int i);
  void SendProgramChange(int channel, int program);
  void SendProgramChangeWithNote(int channel, int program);

  // Static callback for handling delayed note-off messages
  static void NoteOffCallback();

  // Structure to hold note-off information
  struct NoteOffInfo {
    int channel;
    uint8_t note;
    static NoteOffInfo current;
  };

private:
  // Packed parameter storage. Replaces 7 Variable members; per-instance
  // RAM drops from ~330 B to ~120 B (8 * 4 = 32 B params + base-class
  // overhead + pitch-bend state + lastNotes_).
  int32_t params_[kParamCount];
  static_assert(sizeof(params_) == kParamCount * 4,
                "params_ must be tightly packed");

  etl::array<uint8_t, MAX_MIDI_CHORD_NOTES + 1> lastNotes_[SONG_CHANNEL_COUNT];
  int remainingTicks_;
  bool playing_;
  bool retrig_;
  int retrigLoop_;
  char velocity_ = 127;
  TableSaveState tableState_;
  bool first_[SONG_CHANNEL_COUNT];
  uint8_t pitchBendTarget_;
  uint8_t pitchBendSpeed_;
  float pitchBendCurrent_;
  float pitchBendStep_;
  float interpolationAlpha_ = 0.1f;
  bool pitchBend_;
  bool useLogCurve_;

  static MidiService *svc_;
  static TimerService *timerSvc_;
};

// Sized so the etl::pool<MidiInstrument, 16> allocation is bounded; bump
// this ceiling if a future patch legitimately grows the instrument. The
// packed 8-slot params_ array is just 32 B; the rest is base-class
// overhead + per-channel lastNotes_ + pitch-bend state. Pre-migration
// shape was ~330 B (7 Variables × 32 B + 7-vec). The dominant fixed cost
// here is the etl::array<uint8_t, 5> lastNotes_ per channel (8 channels
// = 40 B) + 8 bools (first_) + ~20 B of pitch-bend state.
static_assert(sizeof(MidiInstrument) <= 288,
              "MidiInstrument exceeds stage-5 budget — re-measure params_/"
              "pitch-bend state for unexpected growth");

#endif
