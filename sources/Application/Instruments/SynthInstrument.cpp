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

SynthInstrument::SynthInstrument()
    : I_Instrument(&variables_),
      algorithm_(FourCC::SynthInstrumentAlgorithm, synthAlgorithmNames, 5, 0),
      feedback_(FourCC::SynthInstrumentFeedback, 0),
      feedbackOp_(FourCC::SynthInstrumentFeedbackOp, 0),
      op1Wave_(FourCC::SynthInstrumentOp1Wave, synthWaveNames, SYNTH_WAVE_LAST,
               0),
      op1PW_(FourCC::SynthInstrumentOp1PW, 0x800),
      op1Level_(FourCC::SynthInstrumentOp1Level, 0xFF),
      op1ADSR_(FourCC::SynthInstrumentOp1ADSR, 0x00F8),
      op2Wave_(FourCC::SynthInstrumentOp2Wave, synthWaveNames, 4, 0),
      op2Ratio_(FourCC::SynthInstrumentOp2Ratio, 1),
      op2Detune_(FourCC::SynthInstrumentOp2Detune, 0),
      op2Level_(FourCC::SynthInstrumentOp2Level, 0x00),
      op2ADSR_(FourCC::SynthInstrumentOp2ADSR, 0x00F8),
      op3Wave_(FourCC::SynthInstrumentOp3Wave, synthWaveNames, 4, 0),
      op3Ratio_(FourCC::SynthInstrumentOp3Ratio, 1),
      op3Detune_(FourCC::SynthInstrumentOp3Detune, 0),
      op3Level_(FourCC::SynthInstrumentOp3Level, 0x00),
      op3ADSR_(FourCC::SynthInstrumentOp3ADSR, 0x00F8),
      fltCutoff_(FourCC::SynthInstrumentFilterCutoff, 0xFFF),
      fltReso_(FourCC::SynthInstrumentFilterResonance, 0x0),
      fltMode_(FourCC::SynthInstrumentFilterMode, synthFilterModeNames,
               SYNTH_FLT_LAST, 0),
      fltKeytrack_(FourCC::SynthInstrumentFilterKeytrack, 0x0),
      fltEnvDepth_(FourCC::SynthInstrumentFilterEnvDepth, 0),
      fltADSR_(FourCC::SynthInstrumentFilterADSR, 0x00F8),
      pitchDepth_(FourCC::SynthInstrumentPitchDepth, 0),
      pitchAD_(FourCC::SynthInstrumentPitchAD, 0x00),
      lfoRate_(FourCC::SynthInstrumentLFORate, 0x40),
      lfoShape_(FourCC::SynthInstrumentLFOShape, synthLFOShapeNames,
                SYNTH_LFO_LAST, 0),
      lfoDepth_(FourCC::SynthInstrumentLFODepth, 0x00),
      lfoTarget_(FourCC::SynthInstrumentLFOTarget, synthLFOTargetNames,
                 SYNTH_LFO_TGT_LAST, 1),
      lfoDelay_(FourCC::SynthInstrumentLFODelay, 0x00),
      portamento_(FourCC::SynthInstrumentPortamento, 0x00),
      hardSync_(FourCC::SynthInstrumentHardSync, false),
      ringMod_(FourCC::SynthInstrumentRingMod, false),
      subLevel_(FourCC::SynthInstrumentSubLevel, 0x00),
      volume_(FourCC::SynthInstrumentVolume, 0xC0),
      table_(FourCC::SynthInstrumentTable, -1),
      tableAuto_(FourCC::SynthInstrumentTableAutomation, false) {

  variables_.insert(variables_.end(), &algorithm_);
  variables_.insert(variables_.end(), &feedback_);
  variables_.insert(variables_.end(), &feedbackOp_);
  variables_.insert(variables_.end(), &op1Wave_);
  variables_.insert(variables_.end(), &op1PW_);
  variables_.insert(variables_.end(), &op1Level_);
  variables_.insert(variables_.end(), &op1ADSR_);
  variables_.insert(variables_.end(), &op2Wave_);
  variables_.insert(variables_.end(), &op2Ratio_);
  variables_.insert(variables_.end(), &op2Detune_);
  variables_.insert(variables_.end(), &op2Level_);
  variables_.insert(variables_.end(), &op2ADSR_);
  variables_.insert(variables_.end(), &op3Wave_);
  variables_.insert(variables_.end(), &op3Ratio_);
  variables_.insert(variables_.end(), &op3Detune_);
  variables_.insert(variables_.end(), &op3Level_);
  variables_.insert(variables_.end(), &op3ADSR_);
  variables_.insert(variables_.end(), &fltCutoff_);
  variables_.insert(variables_.end(), &fltReso_);
  variables_.insert(variables_.end(), &fltMode_);
  variables_.insert(variables_.end(), &fltKeytrack_);
  variables_.insert(variables_.end(), &fltEnvDepth_);
  variables_.insert(variables_.end(), &fltADSR_);
  variables_.insert(variables_.end(), &pitchDepth_);
  variables_.insert(variables_.end(), &pitchAD_);
  variables_.insert(variables_.end(), &lfoRate_);
  variables_.insert(variables_.end(), &lfoShape_);
  variables_.insert(variables_.end(), &lfoDepth_);
  variables_.insert(variables_.end(), &lfoTarget_);
  variables_.insert(variables_.end(), &lfoDelay_);
  variables_.insert(variables_.end(), &portamento_);
  variables_.insert(variables_.end(), &hardSync_);
  variables_.insert(variables_.end(), &ringMod_);
  variables_.insert(variables_.end(), &subLevel_);
  variables_.insert(variables_.end(), &volume_);
  variables_.insert(variables_.end(), &table_);
  variables_.insert(variables_.end(), &tableAuto_);
}

SynthInstrument::~SynthInstrument() {}

bool SynthInstrument::Init() {
  SynthVoice::InitTables();
  tableState_.Reset();
  return true;
}

void SynthInstrument::OnStart() { tableState_.Reset(); }

void SynthInstrument::buildParams(VoiceParams &p) {
  p.algorithm = algorithm_.GetInt();
  p.feedback = feedback_.GetInt();
  p.feedback_op = feedbackOp_.GetInt();

  p.op1_wave = op1Wave_.GetInt();
  p.op2_wave = op2Wave_.GetInt();
  p.op3_wave = op3Wave_.GetInt();
  p.op1_pw = op1PW_.GetInt();
  p.op1_level = op1Level_.GetInt();
  p.op2_level = op2Level_.GetInt();
  p.op3_level = op3Level_.GetInt();
  p.op2_ratio = op2Ratio_.GetInt();
  p.op3_ratio = op3Ratio_.GetInt();
  p.op2_detune = (int8_t)op2Detune_.GetInt();
  p.op3_detune = (int8_t)op3Detune_.GetInt();

  uint16_t a1 = op1ADSR_.GetInt();
  uint16_t a2 = op2ADSR_.GetInt();
  uint16_t a3 = op3ADSR_.GetInt();
  uint16_t af = fltADSR_.GetInt();
  p.op1_ad = a1 >> 8;
  p.op1_sr = a1 & 0xFF;
  p.op2_ad = a2 >> 8;
  p.op2_sr = a2 & 0xFF;
  p.op3_ad = a3 >> 8;
  p.op3_sr = a3 & 0xFF;
  p.filt_ad = af >> 8;
  p.filt_sr = af & 0xFF;
  p.pitch_ad = pitchAD_.GetInt() & 0xFF;

  p.filt_cutoff = fltCutoff_.GetInt();
  p.filt_reso = fltReso_.GetInt();
  p.filt_mode = fltMode_.GetInt();
  p.filt_keytrack = fltKeytrack_.GetInt();
  p.filt_env_depth = (int8_t)fltEnvDepth_.GetInt();

  p.pitch_depth = (int8_t)pitchDepth_.GetInt();

  p.lfo_rate = lfoRate_.GetInt();
  p.lfo_shape = lfoShape_.GetInt();
  p.lfo_depth = lfoDepth_.GetInt();
  p.lfo_target = lfoTarget_.GetInt();
  p.lfo_delay = lfoDelay_.GetInt();

  p.porta_rate = portamento_.GetInt();
  p.volume = volume_.GetInt();
  p.sub_level = subLevel_.GetInt();
  p.mod_flags = (hardSync_.GetBool() ? SYNTH_MF_HARDSYNC : 0) |
                (ringMod_.GetBool() ? SYNTH_MF_RINGMOD : 0);
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

int SynthInstrument::GetTable() { return table_.GetInt(); }

bool SynthInstrument::GetTableAutomation() { return tableAuto_.GetBool(); }

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
