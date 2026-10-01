/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "platform.h"
#include "Adapters/sdl/mutex/sdlMutex.h"
#include <SDL3/SDL.h>

SysMutex *platform_mutex() {
  static sdlMutex mutex;
  return &mutex;
}

uint32_t millis(void) { return (uint32_t)SDL_GetTicks(); }

uint32_t micros(void) { return (uint32_t)(SDL_GetTicksNS() / 1000); }
