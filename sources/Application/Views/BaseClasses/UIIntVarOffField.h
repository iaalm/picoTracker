/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_INT_VAR_OFF_FIELD_H_
#define _UI_INT_VAR_OFF_FIELD_H_

#include "UIIntVarField.h"

class UIIntVarOffField : public UIIntVarField {
public:
  // Nullable pointer overload (stage 0.6+). Used by UIParamIntVarOffField.
  UIIntVarOffField(const GUIPoint &position, Variable *v, const char *format,
                   int min, int max, int xOffset, int yOffset);
  // Legacy Variable& overload for existing call sites.
  UIIntVarOffField(const GUIPoint &position, Variable &v, const char *format,
                   int min, int max, int xOffset, int yOffset)
      : UIIntVarOffField(position, &v, format, min, max, xOffset, yOffset) {}
  virtual void ProcessArrow(unsigned short mask);
  virtual void Draw(GUIWindow &w, int offset = 0);
};

#endif
