/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlMutex.h"

// The firmware takes this lock from both the UI and the audio callback, and
// the audio path can re-enter it, hence the recursive mutex.
bool sdlMutex::Lock() {
  mutex_.lock();
  return true;
}

void sdlMutex::Unlock() { mutex_.unlock(); }
