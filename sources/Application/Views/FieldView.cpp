/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "FieldView.h"
#include "Application/AppWindow.h"
#include "System/Console/Trace.h"
#include "UIIntVarField.h"

FieldView::FieldView(GUIWindow &w, ViewData *data) : ScreenView(w, data) {
  focus_ = 0;
  scrollOffset_ = 0;
};

void FieldView::ResetScroll() { scrollOffset_ = 0; }

int FieldView::GetMaxFieldY() const {
  int maxY = kContentTop;
  for (auto it = fieldList_.begin(); it != fieldList_.end(); ++it) {
    int y = (*it)->GetPosition()._y;
    if (y > maxY) {
      maxY = y;
    }
  }
  return maxY;
}

int FieldView::GetMaxScrollOffset() const {
  int maxY = GetMaxFieldY();
  int maxScroll = maxY - kContentBottom;
  return maxScroll > 0 ? maxScroll : 0;
}

void FieldView::EnsureFocusVisible() {
  if (!focus_) {
    return;
  }

  int fy = focus_->GetPosition()._y;
  if (fy < scrollOffset_ + kContentTop) {
    scrollOffset_ = fy - kContentTop;
  }
  if (fy > scrollOffset_ + kContentBottom) {
    scrollOffset_ = fy - kContentBottom;
  }
  if (scrollOffset_ < 0) {
    scrollOffset_ = 0;
  }
  int maxScroll = GetMaxScrollOffset();
  if (scrollOffset_ > maxScroll) {
    scrollOffset_ = maxScroll;
  }
}

void FieldView::DrawScrollBarIfNeeded() {
  const int visibleLines = kContentBottom - kContentTop + 1;
  const int totalLines = GetMaxFieldY() - kContentTop + 1;
  if (totalLines <= visibleLines) {
    return;
  }
  drawScrollBar(SCREEN_WIDTH - 1, kContentTop, visibleLines, scrollOffset_,
                totalLines);
}

void FieldView::SetFocus(UIField *field) {

  if (focus_) {
    focus_->ClearFocus();
  }
  focus_ = field;

  //  Empty field view, we don't have anything to do

  if (focus_ == 0)
    return;

  focus_->SetFocus();
  EnsureFocusVisible();
};

void FieldView::ClearFocus() {
  if (focus_) {
    focus_->ClearFocus();
  };
  focus_ = 0;
};

UIField *FieldView::GetFocus() { return focus_; };

void FieldView::Redraw() {

  if (focus_ == 0 && !fieldList_.empty()) {
    SetFocus(*fieldList_.begin());
  } else {
    EnsureFocusVisible();
  }

  const int drawOffset = -scrollOffset_;
  auto it = fieldList_.begin();
  for (size_t i = 0; i < fieldList_.size(); i++) {
    int drawY = (*it)->GetPosition()._y + drawOffset;
    if (drawY >= kContentTop && drawY <= kContentBottom) {
      (*it)->Draw(w_, drawOffset);
    }
    it++;
  }

  DrawScrollBarIfNeeded();
};

void FieldView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (focus_ == 0) {
    focus_ = *fieldList_.begin();
    //  Empty field view, we don't have anything to do
    if (focus_ == 0)
      return;
    focus_->SetFocus();
  }

  if (mask & EPBM_ENTER) { // ENTER or ENTER+ARROW is sent to the field
    if (mask & EPBM_DOWN) {
      focus_->ProcessArrow(EPBM_DOWN);
      isDirty_ = true;
    }
    if (mask & EPBM_UP) {
      focus_->ProcessArrow(EPBM_UP);
      isDirty_ = true;
    }

    if (mask & EPBM_LEFT) {
      focus_->ProcessArrow(EPBM_LEFT);
      isDirty_ = true;
    }

    if (mask & EPBM_RIGHT) {
      focus_->ProcessArrow(EPBM_RIGHT);
      isDirty_ = true;
    }

    if (mask & EPBM_EDIT) {
      focus_->ProcessClear();
      isDirty_ = true;
    }

    if (mask == EPBM_ENTER) {
      focus_->OnClick();
    };

  } else {
    if (mask & EPBM_EDIT) { // EDIT or EDIT+ARROW is sent to the field

      if (mask == EPBM_EDIT) {
        focus_->OnEditClick();
        isDirty_ = true;
      };

      if (mask & EPBM_DOWN) {
        focus_->ProcessEditArrow(EPBM_DOWN);
        isDirty_ = true;
      }
      if (mask & EPBM_UP) {
        focus_->ProcessEditArrow(EPBM_UP);
        isDirty_ = true;
      }

      if (mask & EPBM_LEFT) {
        focus_->ProcessEditArrow(EPBM_LEFT);
        isDirty_ = true;
      }

      if (mask & EPBM_RIGHT) {
        focus_->ProcessEditArrow(EPBM_RIGHT);
        isDirty_ = true;
      }

    } else { // Nor ENTER or EDIT is pressed

      if (!(mask & (EPBM_ENTER | EPBM_EDIT | EPBM_ALT | EPBM_NAV | EPBM_SELECT |
                    EPBM_PLAY))) {

        if (mask & EPBM_DOWN) {
          UIField *next = 0;
          UIField *first = 0;

          auto it = fieldList_.begin();
          for (size_t i = 0; i < fieldList_.size(); i++) {
            if (!(*it)->IsStatic()) {
              if (first) {
                if ((*it)->GetPosition()._y < first->GetPosition()._y) {
                  first = *it;
                };
              } else {
                first = *it;
              }
              if ((*it)->GetPosition()._y > focus_->GetPosition()._y) {
                if (next) {
                  if ((*it)->GetPosition()._y < next->GetPosition()._y) {
                    next = *it;
                  } else if ((*it)->GetPosition()._y ==
                             next->GetPosition()._y) {
                    // if both targets at same height, prefer the target with an
                    // X value closest to the current focus

                    // cast to signed ints
                    int32_t itX = (*it)->GetPosition()._x;
                    int32_t nextX = next->GetPosition()._x;
                    int32_t focusX = focus_->GetPosition()._x;

                    if (abs(itX - focusX) < abs(nextX - focusX)) {
                      next = *it;
                    }
                  };
                } else {
                  next = *it;
                };
              };
            }
            it++;
          }
          if (next == 0) {
            next = first;
          }

          focus_->ClearFocus();
          focus_ = next;
          focus_->SetFocus();
          EnsureFocusVisible();
          isDirty_ = true;
        }

        if (mask & EPBM_UP) {

          UIField *prev = 0;
          UIField *last = 0;

          auto it = fieldList_.begin();
          for (size_t i = 0; i < fieldList_.size(); i++) {

            if (!(*it)->IsStatic()) {
              if (last) {
                if ((*it)->GetPosition()._y > last->GetPosition()._y) {
                  last = *it;
                };
              } else {
                last = *it;
              }
              if ((*it)->GetPosition()._y < focus_->GetPosition()._y) {
                if (prev) {
                  if ((*it)->GetPosition()._y > prev->GetPosition()._y) {
                    prev = *it;
                  } else if ((*it)->GetPosition()._y ==
                             prev->GetPosition()._y) {
                    // if both targets at same height, prefer the target with an
                    // X value closest to the current focus

                    // cast to signed ints
                    int32_t itX = (*it)->GetPosition()._x;
                    int32_t prevX = prev->GetPosition()._x;
                    int32_t focusX = focus_->GetPosition()._x;

                    if (abs(itX - focusX) < abs(prevX - focusX)) {
                      prev = *it;
                    }
                  };
                } else {
                  prev = *it;
                };
              };
            }
            it++;
          }
          if (prev == 0) {
            prev = last;
          }

          focus_->ClearFocus();
          focus_ = prev;
          focus_->SetFocus();
          EnsureFocusVisible();
          isDirty_ = true;
        }

        if (mask & EPBM_RIGHT) {
          UIField *next = 0;
          UIField *first = 0;

          auto it = fieldList_.begin();
          for (size_t i = 0; i < fieldList_.size(); i++) {

            if (!(*it)->IsStatic() &&
                ((*it)->GetPosition()._y == focus_->GetPosition()._y)) {
              if (first) {
                if ((*it)->GetPosition()._x < first->GetPosition()._x) {
                  first = *it;
                };
              } else {
                first = *it;
              }
              if ((*it)->GetPosition()._x > focus_->GetPosition()._x) {
                if (next) {
                  if ((*it)->GetPosition()._x < next->GetPosition()._x) {
                    next = *it;
                  } else {
                    // if both target at same height
                  };
                } else {
                  next = *it;
                };
              };
            }
            it++;
          }
          if (next == 0) {
            next = first;
          }

          focus_->ClearFocus();
          focus_ = next;
          focus_->SetFocus();
          EnsureFocusVisible();
          isDirty_ = true;
        }

        if (mask & EPBM_LEFT) {

          UIField *prev = 0;
          UIField *last = 0;

          auto it = fieldList_.begin();
          for (size_t i = 0; i < fieldList_.size(); i++) {

            if (!(*it)->IsStatic() &&
                ((*it)->GetPosition()._y == focus_->GetPosition()._y)) {
              if (last) {
                if ((*it)->GetPosition()._x > last->GetPosition()._x) {
                  last = *it;
                };
              } else {
                last = *it;
              }
              if ((*it)->GetPosition()._x < focus_->GetPosition()._x) {
                if (prev) {
                  if ((*it)->GetPosition()._x > prev->GetPosition()._x) {
                    prev = *it;
                  } else {
                    // if both target at same height
                  };
                } else {
                  prev = *it;
                };
              };
            }
            it++;
          }
          if (prev == 0) {
            prev = last;
          }

          focus_->ClearFocus();
          focus_ = prev;
          focus_->SetFocus();
          EnsureFocusVisible();
          isDirty_ = true;
        }
      }
    }
  }
}

int FieldView::GetFocusIndex() {

  int focusIndex = 0;
  auto it = fieldList_.begin();
  for (size_t i = 0; i < fieldList_.size(); i++) {
    if (*it == focus_) {
      break;
    };
    focusIndex++;
    it++;
  };
  return focusIndex;
}
