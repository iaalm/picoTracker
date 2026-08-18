/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "UIParamIntVarField.h"
#include <System/Console/nanoprintf.h>

int UIParamIntVarField::ReadInt() const {
  return instr_ ? instr_->GetParamValue(idx_) : 0;
}

void UIParamIntVarField::WriteInt(int v) {
  if (instr_)
    instr_->SetParamValue(idx_, v);
}

etl::string<MAX_VARIABLE_STRING_LENGTH> UIParamIntVarField::ReadString() const {
  // CHAR_LIST / BOOL parameters render as their label; Draw passes the result
  // to a "%s" format. Everything else is plain decimal.
  if (instr_) {
    const char *label = instr_->GetParamLabel(idx_);
    if (label) {
      return etl::string<MAX_VARIABLE_STRING_LENGTH>(label);
    }
  }
  char buf[16];
  npf_snprintf(buf, sizeof(buf), "%d", ReadInt());
  return etl::string<MAX_VARIABLE_STRING_LENGTH>(buf);
}

Variable::Type UIParamIntVarField::ReadType() const {
  if (instr_ && instr_->IsParamStringTyped(idx_)) {
    // CHAR_LIST and BOOL both take Draw's string branch, which calls
    // ReadString(). CHAR_LIST additionally renders a negative value as
    // "NONE", which is what the "off" sentinel fields want.
    return Variable::CHAR_LIST;
  }
  return Variable::INT;
}

bool UIParamIntVarField::ReadIsModified() const {
  return instr_ && instr_->IsParamModified(idx_);
}

void UIParamIntVarField::ResetVar() {
  if (instr_)
    instr_->ResetParam(idx_);
}

FourCC UIParamIntVarField::GetVariableID() const {
  return instr_ ? instr_->GetParamID(idx_) : FourCC::Default;
}

Variable &UIParamIntVarField::GetVariable() {
  // UIParam* does not own a Variable. Legacy code paths that call
  // GetVariable() expect a live Variable& to SetInt / Reset. Callers using
  // UIParam* fields avoid this path; if reached, we dereference a static
  // sentinel so the call returns something well-defined rather than UB.
  static Variable sentinel(FourCC::Default, 0);
  return sentinel;
}
