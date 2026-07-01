/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_PARAM_BITMASK_VAR_FIELD_H_
#define _UI_PARAM_BITMASK_VAR_FIELD_H_

#include "Application/Instruments/I_Instrument.h"
#include "UIBitmaskVarField.h"

// BitmaskVarField bound to the new (I_Instrument *, idx) parameter API.
// See docs/instrument-param-api.md stage 0.6.
class UIParamBitmaskVarField : public UIBitmaskVarField {
public:
  UIParamBitmaskVarField(const GUIPoint &position, I_Instrument *instr,
                         int idx, const char *format, int len)
      : UIBitmaskVarField(position, (Variable *)nullptr, format, len),
        instr_(instr), idx_(idx) {}

  virtual int ReadInt() const override;
  virtual void WriteInt(int v) override;
  virtual FourCC GetVariableID() const override;

protected:
  I_Instrument *instr_;
  int idx_;
};

#endif
