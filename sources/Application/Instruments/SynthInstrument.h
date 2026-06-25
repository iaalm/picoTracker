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
  etl::ilist<Variable *> *Variables() { return &variables_; };

private:
  // Build a VoiceParams snapshot from the current Variable values.
  void buildParams(VoiceParams &p);

  etl::list<Variable *, 40> variables_;

  // routing
  Variable algorithm_;
  Variable feedback_;
  Variable feedbackOp_;
  // op1
  Variable op1Wave_;
  Variable op1PW_;
  Variable op1Level_;
  Variable op1ADSR_;
  // op2
  Variable op2Wave_;
  Variable op2Ratio_;
  Variable op2Detune_;
  Variable op2Level_;
  Variable op2ADSR_;
  // op3
  Variable op3Wave_;
  Variable op3Ratio_;
  Variable op3Detune_;
  Variable op3Level_;
  Variable op3ADSR_;
  // filter
  Variable fltCutoff_;
  Variable fltReso_;
  Variable fltMode_;
  Variable fltKeytrack_;
  Variable fltEnvDepth_;
  Variable fltADSR_;
  // pitch env
  Variable pitchDepth_;
  Variable pitchAD_;
  // lfo
  Variable lfoRate_;
  Variable lfoShape_;
  Variable lfoDepth_;
  Variable lfoTarget_;
  Variable lfoDelay_;
  // misc
  Variable portamento_;
  Variable hardSync_;
  Variable ringMod_;
  Variable subLevel_;
  Variable volume_;
  Variable table_;
  Variable tableAuto_;

  TableSaveState tableState_;

  // One voice per channel, shared across all SynthInstrument instances.
  // Indexed by channel, never by instrument, so different instruments writing
  // to different channels can never collide.
  static SynthVoice voices_[SONG_CHANNEL_COUNT];
};

#endif
