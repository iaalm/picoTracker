/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

// Shim for the host build -- see the sibling audio/record.h for why this
// directory exists. Provides the bit helpers MacroInstrument.cpp uses,
// without the pico SDK include the device header carries.

#ifndef _UTILS_COMPAT_SHIM_H_
#define _UTILS_COMPAT_SHIM_H_

typedef enum { ERROR = -1, FALSE, TRUE } LOGICAL;

#define BOOL(x) (!(!(x)))

#define BitSet(arg, posn) ((arg) | (1L << (posn)))
#define BitClr(arg, posn) ((arg) & ~(1L << (posn)))
#define BitTst(arg, posn) BOOL((arg) & (1L << (posn)))
#define BitFlp(arg, posn) ((arg) ^ (1L << (posn)))

#endif
