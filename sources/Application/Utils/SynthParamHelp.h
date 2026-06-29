/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * Contextual help legend for KX1 synth instrument parameters.
 * Line 1 max 31 - MAX_BATTERY_GAUGE_WIDTH chars; line 2 max 31 chars.
 */

#ifndef _SYNTH_PARAM_HELP_H_
#define _SYNTH_PARAM_HELP_H_

#include "Foundation/Types/Types.h"

static char **getSynthParamHelp(FourCC param) {
  static char *result[2];
  result[1] = (char *)("                               ");

  switch (param) {
  case FourCC::SynthInstrumentAlgorithm:
    result[0] = (char *)("Algorithm: routing");
    result[1] = (char *)("0-4 op FM topology");
    break;
  case FourCC::SynthInstrumentFeedback:
    result[0] = (char *)("Self-feedback amt");
    result[1] = (char *)("0=off .. 7=max");
    break;
  case FourCC::SynthInstrumentFeedbackOp:
    result[0] = (char *)("Feedback operator");
    result[1] = (char *)("op 0-2 self-mod index");
    break;

  case FourCC::SynthInstrumentOp1Wave:
    result[0] = (char *)("Op1 waveform");
    result[1] = (char *)("sine tri saw pulse noise");
    break;
  case FourCC::SynthInstrumentOp2Wave:
    result[0] = (char *)("Op2 waveform");
    result[1] = (char *)("sine tri saw pulse");
    break;
  case FourCC::SynthInstrumentOp3Wave:
    result[0] = (char *)("Op3 waveform");
    result[1] = (char *)("sine tri saw pulse");
    break;
  case FourCC::SynthInstrumentOp1PW:
    result[0] = (char *)("Op1 pulse width");
    result[1] = (char *)("000-FFF pulse wave only");
    break;
  case FourCC::SynthInstrumentOp2Ratio:
    result[0] = (char *)("Op2 ratio x freq");
    result[1] = (char *)("0=0.5x 1=1x .. 255=255x");
    break;
  case FourCC::SynthInstrumentOp3Ratio:
    result[0] = (char *)("Op3 ratio x freq");
    result[1] = (char *)("0=0.5x 1=1x .. 255=255x");
    break;
  case FourCC::SynthInstrumentOp2Detune:
    result[0] = (char *)("Op2 detune cents");
    result[1] = (char *)("-64~+63 cents ~0.6 st");
    break;
  case FourCC::SynthInstrumentOp3Detune:
    result[0] = (char *)("Op3 detune cents");
    result[1] = (char *)("-64~+63 cents ~0.6 st");
    break;
  case FourCC::SynthInstrumentOp1Level:
    result[0] = (char *)("Op1 level");
    result[1] = (char *)("00-FF carrier/mod depth");
    break;
  case FourCC::SynthInstrumentOp2Level:
    result[0] = (char *)("Op2 level");
    result[1] = (char *)("00-FF modulator level");
    break;
  case FourCC::SynthInstrumentOp3Level:
    result[0] = (char *)("Op3 level");
    result[1] = (char *)("00-FF modulator level");
    break;
  case FourCC::SynthInstrumentOp1ADSR:
    result[0] = (char *)("Op1 env ADSR");
    result[1] = (char *)("A/D/S/R nibble 0-F each");
    break;
  case FourCC::SynthInstrumentOp2ADSR:
    result[0] = (char *)("Op2 env ADSR");
    result[1] = (char *)("A/D/S/R nibble 0-F each");
    break;
  case FourCC::SynthInstrumentOp3ADSR:
    result[0] = (char *)("Op3 env ADSR");
    result[1] = (char *)("A/D/S/R nibble 0-F each");
    break;

  case FourCC::SynthInstrumentFilterCutoff:
    result[0] = (char *)("Filter cutoff");
    result[1] = (char *)("000-FFF clamp ~fs/6");
    break;
  case FourCC::SynthInstrumentFilterResonance:
    result[0] = (char *)("Filter resonance");
    result[1] = (char *)("0-F high=self-osc");
    break;
  case FourCC::SynthInstrumentFilterMode:
    result[0] = (char *)("Filter mode");
    result[1] = (char *)("LP BP HP Notch");
    break;
  case FourCC::SynthInstrumentFilterKeytrack:
    result[0] = (char *)("Filter keytrack");
    result[1] = (char *)("0-F cutoff follows note");
    break;
  case FourCC::SynthInstrumentFilterEnvDepth:
    result[0] = (char *)("Filt env -> cutoff");
    result[1] = (char *)("-128~+127 signed depth");
    break;
  case FourCC::SynthInstrumentFilterADSR:
    result[0] = (char *)("Filter env ADSR");
    result[1] = (char *)("A/D/S/R nibble 0-F each");
    break;

  case FourCC::SynthInstrumentPitchDepth:
    result[0] = (char *)("Pitch env depth");
    result[1] = (char *)("-128~+127 semitones");
    break;
  case FourCC::SynthInstrumentPitchAD:
    result[0] = (char *)("Pitch env A/D");
    result[1] = (char *)("attack/decay 0-F each");
    break;

  case FourCC::SynthInstrumentLFORate:
    result[0] = (char *)("LFO rate");
    result[1] = (char *)("00-FF slow .. fast");
    break;
  case FourCC::SynthInstrumentLFOShape:
    result[0] = (char *)("LFO shape");
    result[1] = (char *)("sine tri saw sq S&H");
    break;
  case FourCC::SynthInstrumentLFODepth:
    result[0] = (char *)("LFO depth");
    result[1] = (char *)("00-FF mod amount");
    break;
  case FourCC::SynthInstrumentLFOTarget:
    result[0] = (char *)("LFO destination");
    result[1] = (char *)("pitch cutoff PW FM amp");
    break;
  case FourCC::SynthInstrumentLFODelay:
    result[0] = (char *)("LFO delay");
    result[1] = (char *)("00=now .. FF=slow fade");
    break;

  case FourCC::SynthInstrumentPortamento:
    result[0] = (char *)("Portamento glide");
    result[1] = (char *)("00=off .. FF=slow slide");
    break;
  case FourCC::SynthInstrumentHardSync:
    result[0] = (char *)("Hard sync on/off");
    result[1] = (char *)("op1 reset when op2 wraps");
    break;
  case FourCC::SynthInstrumentRingMod:
    result[0] = (char *)("Ring modulation");
    result[1] = (char *)("op1 x op2 product");
    break;
  case FourCC::SynthInstrumentSubLevel:
    result[0] = (char *)("Sub osc level");
    result[1] = (char *)("00-FF op1 octave down");
    break;
  case FourCC::SynthInstrumentVolume:
    result[0] = (char *)("Voice volume");
    result[1] = (char *)("00-FF output level");
    break;
  case FourCC::SynthInstrumentTable:
    result[0] = (char *)("Table number");
    result[1] = (char *)("00-FF (-- = none)");
    break;
  case FourCC::SynthInstrumentTableAutomation:
    result[0] = (char *)("Table automation");
    result[1] = (char *)("on/off auto-run table");
    break;

  default:
    result[0] = result[1] = (char *)("");
    break;
  }
  return result;
}

#endif /* _SYNTH_PARAM_HELP_H_ */
