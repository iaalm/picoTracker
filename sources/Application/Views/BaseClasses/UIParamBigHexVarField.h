/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_PARAM_BIG_HEX_VAR_FIELD_H_
#define _UI_PARAM_BIG_HEX_VAR_FIELD_H_

#include "Application/Instruments/I_Instrument.h"
#include "UIBigHexVarField.h"

// BigHexVarField bound to the new (I_Instrument *, idx) parameter API.
// See docs/instrument-param-api.md stage 0.6.
class UIParamBigHexVarField : public UIBigHexVarField {
public:
  UIParamBigHexVarField(const GUIPoint &position, I_Instrument *instr,
                        int idx, int precision, const char *format, int min,
                        int max, int power, bool wrap = false)
      : UIBigHexVarField(position, (Variable *)nullptr, precision, format, min,
                         max, power, wrap),
        instr_(instr), idx_(idx) {}

  virtual int ReadInt() const override;
  virtual void WriteInt(int v) override;
  virtual FourCC GetVariableID() const override;

protected:
  I_Instrument *instr_;
  int idx_;
};

#endif
