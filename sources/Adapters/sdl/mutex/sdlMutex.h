/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_MUTEX_H_
#define _SDL_MUTEX_H_

#include "System/Process/SysMutex.h"
#include <mutex>

class sdlMutex : public SysMutex {
public:
  sdlMutex() {}
  virtual ~sdlMutex(){};
  virtual bool Lock() override;
  virtual void Unlock() override;

private:
  std::recursive_mutex mutex_;
};

#endif // _SDL_MUTEX_H_
