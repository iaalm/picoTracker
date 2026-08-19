/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_INT_VAR_FIELD_H_
#define _UI_INT_VAR_FIELD_H_

#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "UIField.h"

class UIIntVarField : public UIField, public Observable {

public:
  // Nullable-pointer constructor. New in stage 0.6 — passes nullptr for
  // packed-storage subclasses (UIParam*) that own their (I_Instrument *,
  // idx) storage separately and override the virtual accessors below.
  UIIntVarField(const GUIPoint &position, Variable *v, const char *format,
                int min, int max, int xOffset, int yOffset,
                int displayOffset = 0);

  // Legacy Variable& constructor. Retained unchanged so existing
  // fillXxxParameters call sites (which dereference their Variable*) keep
  // working without modification through stage 7.
  UIIntVarField(const GUIPoint &position, Variable &v, const char *format,
                int min, int max, int xOffset, int yOffset,
                int displayOffset = 0)
      : UIIntVarField(position, &v, format, min, max, xOffset, yOffset,
                      displayOffset) {}

  virtual ~UIIntVarField(){};
  virtual void Draw(GUIWindow &w, int offset = 0);
  virtual void ProcessArrow(unsigned short mask);
  virtual void OnClick(){};

  virtual void ProcessClear();
  virtual FourCC GetVariableID() const override;
  virtual Variable &GetVariable();

  // Storage-model-agnostic accessors used by Draw/ProcessArrow/ProcessClear
  // and overridable by packed-storage subclasses (UIParam*) that bypass
  // the legacy Variable. Defaults delegate to src_ when bound.
  virtual int ReadInt() const;
  virtual void WriteInt(int v);
  virtual etl::string<MAX_VARIABLE_STRING_LENGTH> ReadString() const;
  virtual Variable::Type ReadType() const;
  virtual bool ReadIsModified() const;
  virtual void ResetVar();

protected:
  Variable *src_; // may be nullptr for UIParam* subclasses
  const char *format_;
  int min_;
  int max_;
  int xOffset_;
  int yOffset_;
  int displayOffset_;
};

#endif
