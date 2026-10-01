/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_GUI_FACTORY_H_
#define _SDL_GUI_FACTORY_H_

#include "UIFramework/Interfaces/I_GUIWindowFactory.h"

class sdlGUIFactory : public I_GUIWindowFactory {
public:
  sdlGUIFactory();
  virtual I_GUIWindowImp &CreateWindowImp(GUICreateWindowParams &) override;
  virtual EventManager *GetEventManager() override;
};

#endif
