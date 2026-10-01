/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_TIMER_H_
#define _SDL_TIMER_H_

#include "System/Timer/Timer.h"
#include <SDL3/SDL.h>

class sdlTimer : public I_Timer {
public:
  sdlTimer();
  virtual ~sdlTimer();
  virtual void SetPeriod(float msec) override;
  virtual bool Start() override;
  virtual void Stop() override;
  virtual float GetPeriod() override;

  uint32_t OnTimerTick();

private:
  float period_;
  SDL_TimerID timer_;
  bool running_;
};

class sdlTimerService : public TimerService {
public:
  virtual I_Timer *CreateTimer() override;
  virtual void TriggerCallback(int msec, timerCallback cb) override;
};

#endif
