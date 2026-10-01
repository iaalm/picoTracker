/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

// Shim for the host build.
//
// A few core files (AppWindow.cpp, Views/RecordView.cpp) include the device
// adapter by path rather than through an interface. Rather than patch the
// firmware sources, the host build puts this directory first on the include
// path so those includes resolve to the SDL adapter instead.
//
// If that layering leak is ever fixed upstream, this shim can be deleted.

#ifndef _RECORD_COMPAT_SHIM_H_
#define _RECORD_COMPAT_SHIM_H_

#include "Adapters/sdl/audio/record.h"

#endif
