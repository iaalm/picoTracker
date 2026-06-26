/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _FIELD_VIEW_H_
#define _FIELD_VIEW_H_

#include "BaseClasses/UIField.h"
#include "Externals/etl/include/etl/list.h"
#include "ScreenView.h"

class FieldView : public ScreenView {
public:
  FieldView(GUIWindow &w, ViewData *viewData);

  virtual void Redraw();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed) override;

  void SetFocus(UIField *);
  UIField *GetFocus();
  void ClearFocus();
  int GetFocusIndex();
  void ResetScroll();

  etl::list<UIField *, 64> fieldList_; // adjust to maximum fields on one screen
  // ThemeView currently biggest user: uses 64 (12 colors * 5 + font + theme
  // name + buttons)

protected:
  // Scroll the field list vertically when content exceeds the screen.
  // scrollOffset_ is the absolute Y of the topmost visible row.
  void EnsureFocusVisible();
  int GetMaxFieldY() const;
  int GetMaxScrollOffset() const;
  void DrawScrollBarIfNeeded();

  static constexpr int kContentTop = 1; // first row below title bar
  static constexpr int kContentBottom = 23; // SCREEN_HEIGHT - 1
  int scrollOffset_ = 0;

private:
  UIField *focus_;
};

#endif
