/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _HELP_MODAL_H_
#define _HELP_MODAL_H_

#include "ModalView.h"

// Keypad reference, opened by double-tapping NAV from any screen.
//
// The text is a common block (combos that behave the same everywhere)
// followed by a block specific to the screen the help was opened from, so
// what you see matches what the keys under your thumbs actually do.
//
// Lives in BaseClasses rather than ModalDialogs because View::ProcessButton
// opens it, and View.cpp is built into application_views_baseclasses --
// which ModalDialogs links against, not the other way round.
class HelpModal : public ModalView {
public:
  // Only one help modal exists at a time; this reuses a static buffer, like
  // MessageBox::Create. The firmware is built without a heap.
  static HelpModal *Create(View &view, ViewType forView);
  virtual ~HelpModal();
  virtual void Destroy() override;

  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick){};
  virtual void OnFocus(){};
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void AnimationUpdate(){};

protected:
  HelpModal(View &view, ViewType forView);

private:
  // Total lines across the common and per-view blocks.
  int LineCount() const;
  // Resolves a flat index onto one of the two blocks.
  const char *LineAt(int index) const;

  const char *const *viewLines_;
  int viewLineCount_;
  int topLine_;

  static bool inUse_;
  static void *storage_;
};
#endif
