/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "UIIntVarField.h"

#include "Application/AppWindow.h"
#include "System/Console/Trace.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include "UIIntVarField.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <string.h>

#define abs(x) (x < 0 ? -x : x)

UIIntVarField::UIIntVarField(const GUIPoint &position, Variable *v,
                             const char *format, int min, int max, int xOffset,
                             int yOffset, int displayOffset)
    : UIField(position), src_(v) {
  format_ = format;
  min_ = min;
  max_ = max;
  xOffset_ = xOffset;
  yOffset_ = yOffset;
  displayOffset_ = displayOffset;
};

void UIIntVarField::Draw(GUIWindow &w, int offset) {

  GUITextProperties props;
  GUIPoint position = GetPosition();
  position._y += offset;

  Variable::Type type = ReadType();
  char buffer[MAX_FIELD_WIDTH + 1];

  // format_ and the variable's type are set independently -- the format comes
  // from the fill*Parameters() call that built the field, the type from
  // whatever Variable or packed parameter it ended up bound to. A refresh
  // that rebinds a field can leave the two disagreeing, and handing an int to
  // a "%s" makes nanoprintf dereference it as a pointer. That is a segfault,
  // not a cosmetic glitch, so trust the format string over the type.
  const bool wantsString = strstr(format_, "%s") != nullptr;

  switch (type) {
  case Variable::INT: {
    if (wantsString) {
      // Mismatch: render the number rather than crash, and make it visible
      // that something is wrong rather than silently printing a plausible
      // value.
      int ivalue = ReadInt() + displayOffset_;
      npf_snprintf(buffer, sizeof(buffer), "?%d", ivalue);
      break;
    }
    int ivalue = ReadInt() + displayOffset_;
    npf_snprintf(buffer, sizeof(buffer), format_, ivalue, ivalue);
  } break;
  case Variable::CHAR_LIST:
    if (!wantsString) {
      npf_snprintf(buffer, sizeof(buffer), format_, ReadInt(), ReadInt());
      break;
    }
    // if no value initialize with "NONE"
    if (ReadInt() < 0) {
      npf_snprintf(buffer, sizeof(buffer), format_, "NONE");
    } else {
      auto value = ReadString();
      const char *cvalue = value.c_str();
      npf_snprintf(buffer, sizeof(buffer), format_, cvalue);
    }
    break;
  case Variable::BOOL: {
    if (!wantsString) {
      npf_snprintf(buffer, sizeof(buffer), format_, ReadInt(), ReadInt());
      break;
    }
    auto value = ReadString();
    const char *cvalue = value.c_str();
    npf_snprintf(buffer, sizeof(buffer), format_, cvalue);
  } break;

  default:
    strcpy(buffer, "++wtf++");
  }

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer);
  }
};

void UIIntVarField::ProcessArrow(unsigned short mask) {
  int value = ReadInt();

  switch (mask) {
  case EPBM_UP:
    value += yOffset_;
    break;
  case EPBM_DOWN:
    value -= yOffset_;
    break;
  case EPBM_LEFT:
    value -= xOffset_;
    break;
  case EPBM_RIGHT:
    value += xOffset_;
    break;
  };
  if (value < min_) {
    value = min_;
  };
  if (value > max_) {
    value = max_;
  }

  WriteInt(value);

  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(GetVariableID())));
};

void UIIntVarField::ProcessClear() {
  if (!ReadIsModified())
    return;

  ResetVar();

  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(GetVariableID())));
};

FourCC UIIntVarField::GetVariableID() const {
  return src_ ? src_->GetID() : FourCC::Default;
};

Variable &UIIntVarField::GetVariable() { return *src_; };

// ---------------------------------------------------------------------------
// Storage-model-agnostic accessors. Default implementations delegate to the
// bound Variable. UIParam* subclasses (stage 0.6) override these to read
// from / write to a packed (I_Instrument *, idx) pair instead.
// ---------------------------------------------------------------------------

int UIIntVarField::ReadInt() const { return src_ ? src_->GetInt() : 0; }

void UIIntVarField::WriteInt(int v) {
  if (src_)
    src_->SetInt(v);
}

etl::string<MAX_VARIABLE_STRING_LENGTH> UIIntVarField::ReadString() const {
  return src_ ? src_->GetString() : etl::string<MAX_VARIABLE_STRING_LENGTH>();
}

Variable::Type UIIntVarField::ReadType() const {
  return src_ ? src_->GetType() : Variable::INT;
}

bool UIIntVarField::ReadIsModified() const {
  return src_ && src_->IsModified();
}

void UIIntVarField::ResetVar() {
  if (src_)
    src_->Reset();
}
