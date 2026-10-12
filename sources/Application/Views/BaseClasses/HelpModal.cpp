/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "HelpModal.h"
#include "Application/AppWindow.h"
#include "Application/Player/Player.h"
#include <new>

// ModalView::SetWindow clamps to 28x20, so that -- not SCREEN_WIDTH -- is
// what the text has to fit into. Rows: 0 title, 1 blank, 2..17 content,
// 18 blank, 19 footer.
#define HELP_WIDTH 28
#define HELP_HEIGHT 20
#define HELP_FIRST_ROW 2
#define HELP_VISIBLE_LINES 16
#define HELP_FOOTER_ROW 19

// Entries are drawn verbatim; keep them inside HELP_WIDTH. A leading '\x01'
// marks a section heading (drawn in the accent colour, not as a binding).
#define HEAD "\x01"

static const char *const commonLines[] = {
    HEAD "ANY SCREEN",
    "ENTER      insert value",
    "ENTER,ENTER  next unused",
    "EDIT+ENTER cut value",
    "ALT+ENTER  cut / paste",
    "ALT+EDIT   start select",
};

// Once a selection is open the keys mean something else -- processed by a
// separate handler (e.g. SongView::processSelectionButtonMask), where bare
// EDIT copies instead of doing nothing. Listing them beside the normal
// bindings read as if they were always live.
// One binding per line, as in the blocks above; clang-format would otherwise
// pack this one two-per-line because the strings are shorter.
// clang-format off
static const char *const selectionLines[] = {
    HEAD "WHILE SELECTING",
    "ALT+EDIT   grow row/screen",
    "EDIT       copy selection",
    "ALT+ENTER  cut selection",
};
// clang-format on

// The song screen is two screens wearing one coat: EDIT+L/R flips the
// sequencer between song and live mode, and PLAY, EDIT+PLAY and NAV+PLAY
// then do entirely different things (see Player::OnSongStartButton). Showing
// both sets at once would mean half the block is wrong whichever mode you
// are in, so the help picks the block for the mode you are actually in.
static const char *const songModeLines[] = {
    HEAD "SONG - SONG MODE",
    "PLAY       start / stop",
    "EDIT+PLAY  record screen",
    "ALT+PLAY   live from row",
    "ALT+L/R    nudge tempo",
    "ALT+U/D    next filled row",
    "EDIT+U/D   page up / down",
    "EDIT+L/R   go live mode",
    "NAV+EDIT   mute track",
    "NAV+ENTER  solo track",
    "NAV+ALT    unmute all",
};

static const char *const liveModeLines[] = {
    HEAD "SONG - LIVE MODE",
    "PLAY       queue chain",
    "EDIT+PLAY  start now",
    "NAV+PLAY   stop at end",
    "ALT+PLAY   all from row",
    "ALT+L/R    nudge tempo",
    "ALT+U/D    next filled row",
    "EDIT+U/D   page up / down",
    "EDIT+L/R   go song mode",
    "NAV+EDIT   mute track",
    "NAV+ENTER  solo track",
    "NAV+ALT    unmute all",
};

static const char *const chainLines[] = {
    HEAD "CHAIN",
    "EDIT+L/R   prev/next chan",
    "EDIT+U/D   prev/next chain",
    "EDIT+ALT,ALT+ENTER clone",
    "NAV+PLAY   play in song",
};

static const char *const phraseLines[] = {
    HEAD "PHRASE",
    "ENTER hold audition note",
    "EDIT+L/R   prev/next track",
    "EDIT+U/D   prev/next phrase",
    "EDIT+ALT,ALT+ENTER clone",
    "NAV+PLAY   play in song",
};

static const char *const instrumentLines[] = {
    HEAD "INSTRUMENT",
    "EDIT+L/R   instr -/+ 1",
    "EDIT+U/D   instr -/+ 16",
    "ENTER+EDIT reset to default",
    "EDIT+ALT,ALT+ENTER clone",
};

static const char *const tableLines[] = {
    HEAD "TABLE",
    "EDIT+L/R   table -/+ 1",
    "EDIT+U/D   table -/+ 16",
};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

bool HelpModal::inUse_ = false;
alignas(HelpModal) static unsigned char HelpModalStorage[sizeof(HelpModal)];
void *HelpModal::storage_ = HelpModalStorage;

HelpModal *HelpModal::Create(View &view, ViewType forView) {
  if (inUse_) {
    auto *existing = reinterpret_cast<HelpModal *>(storage_);
    existing->~HelpModal();
    inUse_ = false;
  }
  inUse_ = true;
  return new (storage_) HelpModal(view, forView);
}

HelpModal::HelpModal(View &view, ViewType forView)
    : ModalView(view), viewLines_(nullptr), viewLineCount_(0), topLine_(0) {
  switch (forView) {
  case VT_SONG: {
    // Read the mode once, when the help opens -- it cannot change while a
    // modal has the keys.
    bool live = Player::GetInstance()->GetSequencerMode() == SM_LIVE;
    viewLines_ = live ? liveModeLines : songModeLines;
    viewLineCount_ = live ? COUNT_OF(liveModeLines) : COUNT_OF(songModeLines);
    break;
  }
  case VT_CHAIN:
    viewLines_ = chainLines;
    viewLineCount_ = COUNT_OF(chainLines);
    break;
  case VT_PHRASE:
    viewLines_ = phraseLines;
    viewLineCount_ = COUNT_OF(phraseLines);
    break;
  case VT_INSTRUMENT:
    viewLines_ = instrumentLines;
    viewLineCount_ = COUNT_OF(instrumentLines);
    break;
  case VT_TABLE:  // under phrase
  case VT_TABLE2: // under instrument
    viewLines_ = tableLines;
    viewLineCount_ = COUNT_OF(tableLines);
    break;
  default:
    // Screens without their own block still get the common one.
    break;
  }
}

HelpModal::~HelpModal(){};

void HelpModal::Destroy() {
  this->~HelpModal();
  inUse_ = false;
}

int HelpModal::LineCount() const {
  return viewLineCount_ + COUNT_OF(commonLines) + COUNT_OF(selectionLines);
}

const char *HelpModal::LineAt(int index) const {
  // The screen-specific block comes first: it is the reason the help was
  // opened from this screen rather than another.
  if (index < viewLineCount_) {
    return viewLines_[index];
  }
  index -= viewLineCount_;
  if (index < COUNT_OF(commonLines)) {
    return commonLines[index];
  }
  // The selection bindings go last, under their own heading: they only
  // apply once a selection is open.
  return selectionLines[index - COUNT_OF(commonLines)];
}

void HelpModal::DrawView() {
  SetWindow(HELP_WIDTH, HELP_HEIGHT);

  GUITextProperties props;
  const int total = LineCount();

  SetColor(CD_INFO);
  DrawString(0, 0, "KEYS", props);

  for (int row = 0; row < HELP_VISIBLE_LINES; row++) {
    const int index = topLine_ + row;
    if (index >= total) {
      break;
    }
    const char *line = LineAt(index);
    if (line[0] == HEAD[0]) {
      SetColor(CD_ACCENT);
      DrawString(0, HELP_FIRST_ROW + row, line + 1, props);
    } else {
      SetColor(CD_NORMAL);
      DrawString(0, HELP_FIRST_ROW + row, line, props);
    }
  }

  SetColor(CD_INFO);
  // Only promise scrolling when there is something to scroll to.
  DrawString(0, HELP_FOOTER_ROW,
             total > HELP_VISIBLE_LINES ? "up/down: more   any: close"
                                        : "any key: close",
             props);
  SetColor(CD_NORMAL);
}

void HelpModal::ProcessButtonMask(unsigned short mask, bool pressed) {
  // Act on press only: the release of whichever key closes this would
  // otherwise be handled a second time.
  if (!pressed) {
    return;
  }

  const int total = LineCount();
  const int maxTop = total - HELP_VISIBLE_LINES;

  if (total > HELP_VISIBLE_LINES) {
    if (mask == EPBM_DOWN) {
      if (topLine_ < maxTop) {
        topLine_++;
        isDirty_ = true;
      }
      return;
    }
    if (mask == EPBM_UP) {
      if (topLine_ > 0) {
        topLine_--;
        isDirty_ = true;
      }
      return;
    }
  }

  EndModal(0);
}
