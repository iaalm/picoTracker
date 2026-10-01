/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_MIDI_SERVICE_H_
#define _SDL_MIDI_SERVICE_H_

#include "Services/Midi/MidiService.h"

// The emulator registers no MIDI devices, so queued messages are simply
// dropped at flush time. The sequencer still runs its MIDI timing path, which
// is what matters for reproducing tracker behaviour; routing to real ports is
// left for a later pass.
class sdlMidiService : public MidiService {
public:
  sdlMidiService() {}
  ~sdlMidiService() {}
};

#endif
