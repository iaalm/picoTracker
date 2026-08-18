/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "UIParamBigHexVarField.h"

int UIParamBigHexVarField::ReadInt() const {
  return instr_ ? instr_->GetParamValue(idx_) : 0;
}

void UIParamBigHexVarField::WriteInt(int v) {
  if (instr_)
    instr_->SetParamValue(idx_, v);
}

FourCC UIParamBigHexVarField::GetVariableID() const {
  return instr_ ? instr_->GetParamID(idx_) : FourCC::Default;
}
