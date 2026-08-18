/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "UIParamIntVarOffField.h"

int UIParamIntVarOffField::ReadInt() const {
  return instr_ ? instr_->GetParamValue(idx_) : 0;
}

void UIParamIntVarOffField::WriteInt(int v) {
  if (instr_)
    instr_->SetParamValue(idx_, v);
}

FourCC UIParamIntVarOffField::GetVariableID() const {
  return instr_ ? instr_->GetParamID(idx_) : FourCC::Default;
}
