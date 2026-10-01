/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

// Host stand-in for the device's platform.h. The core only needs
// platform_mutex() from this header; the rest of the device's platform API is
// hardware bring-up that has no host equivalent.

#ifndef _PLATFORM_SDL_H_
#define _PLATFORM_SDL_H_

#include "System/Process/SysMutex.h"
#include <stdint.h>

SysMutex *platform_mutex();

uint32_t millis(void);
uint32_t micros(void);

#endif
