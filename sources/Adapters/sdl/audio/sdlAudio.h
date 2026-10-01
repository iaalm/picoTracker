/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_AUDIO_H_
#define _SDL_AUDIO_H_

#include "Services/Audio/Audio.h"

class sdlAudio : public Audio {
public:
  sdlAudio(AudioSettings &hints);
  ~sdlAudio();
  virtual void Init() override;
  virtual void Close() override;
  virtual int GetMixerVolume() override;
  virtual void SetMixerVolume(int volume) override;

private:
  AudioSettings hints_;
};

#endif
