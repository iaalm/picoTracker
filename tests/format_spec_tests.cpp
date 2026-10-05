/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Application/Utils/FormatSpec.h"
#include "doctest/doctest.h"

// FormatWantsString decides whether UIIntVarField::Draw passes a char * or
// an int to nanoprintf. Getting it wrong is a segfault rather than a
// mis-render, and the first version of it -- a plain strstr(fmt, "%s") --
// did exactly that: it missed "sample: %.17s", so the sample field was fed
// an int and the instrument screen crashed on open.
TEST_CASE("FormatWantsString recognises string conversions") {
  CHECK(FormatWantsString("Type: %s"));
  CHECK(FormatWantsString("automation: %s"));
  CHECK(FormatWantsString("%s"));
  // The case the strstr version missed: a precision between % and s.
  CHECK(FormatWantsString("sample: %.17s"));
  CHECK(FormatWantsString("%-10s"));
  CHECK(FormatWantsString("%.*s"));
}

TEST_CASE("FormatWantsString rejects numeric conversions") {
  // Every numeric format the instrument screens actually use.
  CHECK_FALSE(FormatWantsString("volume: %2.2X"));
  CHECK_FALSE(FormatWantsString("channel: %2.2d"));
  CHECK_FALSE(FormatWantsString("%1.1X"));
  CHECK_FALSE(FormatWantsString("%4.4X"));
  CHECK_FALSE(FormatWantsString("%7.7X"));
  CHECK_FALSE(FormatWantsString("%3.3X"));
  CHECK_FALSE(FormatWantsString("%04b"));
  CHECK_FALSE(FormatWantsString("%02b"));
  CHECK_FALSE(FormatWantsString("count: %d"));
}

TEST_CASE("FormatWantsString handles degenerate input") {
  CHECK_FALSE(FormatWantsString(""));
  CHECK_FALSE(FormatWantsString(nullptr));
  CHECK_FALSE(FormatWantsString("no conversions here"));
  // A literal %% is not a conversion; the %d after it is what counts.
  CHECK_FALSE(FormatWantsString("100%% sure"));
  CHECK(FormatWantsString("100%% of %s"));
  // Trailing % with nothing after it must not read past the end.
  CHECK_FALSE(FormatWantsString("%"));
  CHECK_FALSE(FormatWantsString("width only: %12"));
}
