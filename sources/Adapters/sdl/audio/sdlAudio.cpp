/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlAudio.h"
#include "Services/Audio/AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "sdlAudioDriver.h"

sdlAudio::sdlAudio(AudioSettings &hints) : Audio(hints) { hints_ = hints; }

sdlAudio::~sdlAudio() {}

void sdlAudio::Init() {
  AudioSettings settings;
  settings.audioAPI_ = GetAudioAPI();
  settings.bufferSize_ = GetAudioBufferSize();
  settings.preBufferCount_ = GetAudioPreBufferCount();

  alignas(sdlAudioDriver) static char audioDriver[sizeof(sdlAudioDriver)];
  sdlAudioDriver *drv = new (audioDriver) sdlAudioDriver(settings);
  alignas(AudioOutDriver) static char audioOutDriver[sizeof(AudioOutDriver)];
  AudioOutDriver *out = new (audioOutDriver) AudioOutDriver(*drv);
  AddOutput(*out);
}

void sdlAudio::Close() {
  for (auto *out : Outputs()) {
    if (out) {
      out->Close();
    }
  }
}

void sdlAudio::SetMixerVolume(int v) {
  AudioOutDriver *out = (AudioOutDriver *)GetFirstOutput();
  if (out) {
    ((sdlAudioDriver *)out->GetDriver())->SetVolume(v);
  }
}

int sdlAudio::GetMixerVolume() {
  AudioOutDriver *out = (AudioOutDriver *)GetFirstOutput();
  if (out) {
    return ((sdlAudioDriver *)out->GetDriver())->GetVolume();
  }
  return 0;
}
