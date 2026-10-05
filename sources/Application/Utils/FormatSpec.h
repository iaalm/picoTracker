/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _FORMAT_SPEC_H_
#define _FORMAT_SPEC_H_

#include <string.h>

// Does this format string's first conversion consume a char *?
//
// UIIntVarField::Draw picks which printf argument to pass from the bound
// variable's type, while format_ comes from whichever fill*Parameters()
// built the field. Nothing keeps the two in step, and passing an int where
// the format says %s makes nanoprintf dereference it -- a segfault, not a
// cosmetic glitch. Draw therefore trusts the format string, and this is how
// it reads one.
//
// A substring search for "%s" is not enough: the field formats in use
// include "sample: %.17s", where a precision sits between the % and the s,
// alongside "%2.2X", "%04b" and "%1.1X". So walk the flags, width and
// precision to reach the conversion character.
//
// Header-only so host tests can exercise it without pulling in the UI
// stack; see tests/format_spec_tests.cpp.
inline bool FormatWantsString(const char *fmt) {
  if (!fmt) {
    return false;
  }
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') {
      continue;
    }
    p++;
    if (*p == '%') { // literal %%, not a conversion
      continue;
    }
    while (*p && (strchr("-+ #0", *p) || (*p >= '0' && *p <= '9') ||
                  *p == '.' || *p == '*')) {
      p++;
    }
    if (!*p) {
      return false;
    }
    return *p == 's';
  }
  return false;
}

#endif // _FORMAT_SPEC_H_
