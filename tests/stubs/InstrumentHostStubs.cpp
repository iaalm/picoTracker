// Host-side stubs for the I_Instrument link-time dependencies the production
// firmware pulls in via DSP / service / timing code (Opal OPL3 emulator,
// cRSID SID emulator, Profiler, TableSaveState::Reset).
//
// We do NOT exercise any of these code paths in the persistence tests — the
// tests only read the static NAMES / SPECS / FORMATS tables and call
// GetParamName / GetParamCount / FindParamByName. The constructors and statics
// from the instrument .cpp files nevertheless reference these symbols (e.g.
// SIDInstrument's static cRSID members, OpalInstrument's Opal member,
// MidiInstrument's MidiService lookup). These stubs satisfy the linker; they
// are intentionally trivial because the real implementations are
// RP2040/DSP-bound and have no host value.
//
// T_Factory<T>::GetInstance (MidiService / TimerService) is already provided
// by Foundation/T_Factory.cpp — no stub needed there.

#include "Application/Player/TablePlayback.h"
#include "Application/Utils/fixed.h"
#include "Externals/cRSID/SID.h"
#include "Externals/opal/opal.h"
#include "Services/Midi/MidiMessage.h"
#include "Services/Midi/MidiService.h"
#include "System/Profiler/Profiler.h"

// ---- MidiService: abstract methods MidiInstrument.o references ----
void MidiService::RegisterActiveChannel(uint8_t /*channel*/) {}
void MidiService::QueueMessage(MidiMessage & /*msg*/) {}

// ---- cRSID (SID emulator) ----
cRSID::cRSID(unsigned short /*samplerate*/) {
  // Firmware version constructs OPL lookup tables etc.; host test only needs
  // the symbol to exist so static SIDInstrument::sid1_/sid2_ can initialize.
  SampleClockRatio = 0;
  for (int i = 0; i < 29; i++)
    Register[i] = 0;
}
void cRSID::cRSID_emulateADSRs(char /*cycles*/) {}
void cRSID::cRSID_resetChannel(unsigned char /*channel*/) {}
int cRSID::cRSID_emulateWaves() { return 0; }
cRSID_SIDwavOutput cRSID::cRSID_emulateHQwaves(char /*cycles*/) {
  return {0, 0};
}
void cRSID::cRSID_emulateWavesBuffer(fixed * /*buffer*/, int /*size*/) {}

// ---- Opal (OPL3 emulator) ----
Opal::Opal(int /*sample_rate*/) {
  SampleRate = 0;
  SampleAccum = 0;
  LastOutput[0] = LastOutput[1] = 0;
  CurrOutput[0] = CurrOutput[1] = 0;
  Clock = 0;
}
Opal::~Opal() = default;
void Opal::SetSampleRate(int /*sample_rate*/) {}
void Opal::Port(uint16_t /*reg_num*/, uint8_t /*val*/) {}
void Opal::Sample(int16_t * /*left*/, int16_t * /*right*/) {}
void Opal::SampleBuffer(fixed * /*buffer*/, int /*size*/) {}
void Opal::Init(int /*sample_rate*/) {}
void Opal::Output(int16_t & /*left*/, int16_t & /*right*/) {}

// ---- Profiler ----
Profiler::Profiler(char const *) {}
Profiler::~Profiler() {}

// ---- TableSaveState::Reset (Player/Table) ----
void TableSaveState::Reset() {
  for (int r = 0; r < TABLE_STEPS; r++)
    for (int c = 0; c < TABLE_COLUMNS; c++)
      hopCount_[r][c] = 0;
  for (int c = 0; c < TABLE_COLUMNS; c++)
    position_[c] = 0;
  groove_ = {};
}
