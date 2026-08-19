/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_FIELD_H_
#define _UI_FIELD_H_

#include "Foundation/Types/Types.h"
#include "System/Console/Trace.h"
#include "UIFramework/BasicDatas/GUIPoint.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "View.h"

class UIField {
public:
  UIField(const GUIPoint &position);
  virtual ~UIField();
  virtual void Draw(GUIWindow &w, int offset = 0) = 0;
  virtual void OnClick() = 0; // ENTER pressed
  virtual void ProcessArrow(unsigned short mask) = 0;
  virtual void OnEditClick(){}; // EDIT pressed
  virtual void ProcessEditArrow(unsigned short mask){};
  virtual void ProcessClear(){}; // EDIT+ENTER pressed
  void SetFocus();
  void ClearFocus();
  bool HasFocus();
  void SetPosition(GUIPoint &);
  GUIPoint GetPosition();
  GUIColor GetColor();

  virtual bool IsStatic();

  // Identifies the parameter a field edits, or Default for fields that edit
  // nothing (static labels, action buttons).
  //
  // This MUST live on the base class. Callers hold UIField* for a
  // heterogeneous list, and the only way to ask "which parameter is this?"
  // used to be a C-style cast to UIIntVarField*. For a UIStaticField — which
  // declares no virtuals of its own — GetVariableID() sits past the end of
  // its vtable, so that cast called whatever bytes happened to follow the
  // vtable in flash. It is undefined behaviour that happens to survive only
  // as long as the link layout stays lucky.
  virtual FourCC GetVariableID() const { return FourCC::Default; }

protected:
  uint8_t x_;
  uint8_t y_;
  bool focus_;
};
#endif
