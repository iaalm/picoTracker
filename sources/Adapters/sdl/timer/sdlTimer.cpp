/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlTimer.h"
#include "System/Console/Trace.h"

// On the device this runs in an alarm interrupt; here it runs on SDL's timer
// thread. Either way it is asynchronous with respect to the main loop, which
// is what the observers expect.
static uint32_t sdlTimerCallback(void *param, SDL_TimerID id,
                                 uint32_t interval) {
  sdlTimer *timer = (sdlTimer *)param;
  return timer->OnTimerTick();
}

static uint32_t sdlTriggerCallback(void *param, SDL_TimerID id,
                                   uint32_t interval) {
  timerCallback tc = (timerCallback)param;
  (*tc)();
  return 0; // one-shot
}

sdlTimer::sdlTimer() : period_(-1), timer_(0), running_(false) {}

sdlTimer::~sdlTimer() { Stop(); }

void sdlTimer::SetPeriod(float msec) { period_ = msec; }

bool sdlTimer::Start() {
  if (period_ > 0) {
    uint32_t interval = (uint32_t)period_;
    if (interval == 0) {
      interval = 1; // SDL treats 0 as "stop"; the device clamps similarly
    }
    running_ = true;
    timer_ = SDL_AddTimer(interval, sdlTimerCallback, this);
  }
  return (timer_ != 0);
}

void sdlTimer::Stop() {
  running_ = false;
  if (timer_ != 0) {
    SDL_RemoveTimer(timer_);
    timer_ = 0;
  }
}

float sdlTimer::GetPeriod() { return period_; }

uint32_t sdlTimer::OnTimerTick() {
  if (!running_) {
    return 0;
  }
  SetChanged();
  NotifyObservers();
  uint32_t next = (uint32_t)period_;
  return next == 0 ? 1 : next;
}

I_Timer *sdlTimerService::CreateTimer() {
  // Single shared instance, matching the device adapter.
  static sdlTimer timerInstance;
  timerInstance.Stop();
  timerInstance.SetPeriod(-1.0f);
  return &timerInstance;
}

void sdlTimerService::TriggerCallback(int msec, timerCallback cb) {
  SDL_AddTimer(msec, sdlTriggerCallback, (void *)cb);
}
