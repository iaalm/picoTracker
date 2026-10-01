/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_AUDIO_DRIVER_H_
#define _SDL_AUDIO_DRIVER_H_

#include "Services/Audio/AudioDriver.h"
#include <SDL3/SDL.h>

class sdlAudioDriver : public AudioDriver {
public:
  sdlAudioDriver(AudioSettings &settings);
  virtual ~sdlAudioDriver();

  virtual bool InitDriver() override;
  virtual void CloseDriver() override;
  virtual bool StartDriver() override;
  virtual void StopDriver() override;
  virtual int GetPlayedBufferPercentage() override;
  virtual bool Interlaced() override { return true; };
  virtual double GetStreamTime() override;

  void SetVolume(int v);
  int GetVolume();

private:
  // SDL pulls from this whenever the device needs more audio; it stands in
  // for the device's DMA-completion interrupt.
  static void SDLCALL AudioCallback(void *userdata, SDL_AudioStream *stream,
                                    int additional_amount, int total_amount);
  void onBufferNeeded(SDL_AudioStream *stream, int additionalBytes);

  SDL_AudioStream *stream_;
  int volume_;
  uint64_t startTime_;
};

#endif
