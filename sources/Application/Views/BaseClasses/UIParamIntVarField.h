/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_PARAM_INT_VAR_FIELD_H_
#define _UI_PARAM_INT_VAR_FIELD_H_

#include "Application/Instruments/I_Instrument.h"
#include "UIIntVarField.h"

// UIIntVarField bound to the new (I_Instrument *, idx) parameter API instead
// of a legacy Variable&. Inherits the parent's draw/scroll/input behaviour
// via the storage-model-agnostic virtual accessors (ReadInt/WriteInt/...).
//
// See docs/instrument-param-api.md stage 0.6.
class UIParamIntVarField : public UIIntVarField {
public:
  UIParamIntVarField(const GUIPoint &position, I_Instrument *instr, int idx,
                     const char *format, int min, int max, int xOffset,
                     int yOffset, int displayOffset = 0)
      : UIIntVarField(position, (Variable *)nullptr, format, min, max,
                      xOffset, yOffset, displayOffset),
        instr_(instr), idx_(idx) {}

  // --- Storage-model accessors. Override the parent's Variable-based defaults
  // to read/write the instrument's packed parameter array via the new API.
  virtual int ReadInt() const override;
  virtual void WriteInt(int v) override;
  virtual etl::string<MAX_VARIABLE_STRING_LENGTH> ReadString() const override;
  // Parameters whose UI format is "%s" (waveform, algorithm, filter mode and
  // the BOOL toggles) must report a string type, otherwise
  // UIIntVarField::Draw formats the raw int through a "%s" template and
  // dereferences it as a char *. The instrument's label table is the single
  // source of truth for which parameters those are.
  virtual Variable::Type ReadType() const override;
  virtual bool ReadIsModified() const override;
  virtual void ResetVar() override;
  virtual FourCC GetVariableID() const override;
  virtual Variable &GetVariable() override;

protected:
  I_Instrument *instr_;
  int idx_;
};

#endif
