/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlGUIFactory.h"
#include "sdlEventManager.h"
#include "sdlGUIWindowImp.h"

sdlGUIFactory::sdlGUIFactory() {}

I_GUIWindowImp &sdlGUIFactory::CreateWindowImp(GUICreateWindowParams &p) {
  // Statically allocated like the device adapter: the firmware is built with
  // dynamic allocation disabled.
  alignas(sdlGUIWindowImp) static char buf[sizeof(sdlGUIWindowImp)];
  return *(new (buf) sdlGUIWindowImp(p));
}

EventManager *sdlGUIFactory::GetEventManager() {
  alignas(sdlEventManager) static char buf[sizeof(sdlEventManager)];
  static EventManager *instance = new (buf) sdlEventManager();
  return instance;
}
