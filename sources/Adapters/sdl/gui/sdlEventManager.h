/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_EVENT_MANAGER_H_
#define _SDL_EVENT_MANAGER_H_

#include "UIFramework/SimpleBaseClasses/EventManager.h"
#include <stdint.h>

// Same bit layout as the device keypad (Adapters/picoTracker/system/input.h)
// so the shared button-dispatch code can be reused unchanged.
enum SDLKeypadBits {
  SDLKEY_LEFT = 1 << 0,
  SDLKEY_DOWN = 1 << 1,
  SDLKEY_RIGHT = 1 << 2,
  SDLKEY_UP = 1 << 3,
  SDLKEY_ALT = 1 << 4,
  SDLKEY_EDIT = 1 << 5,
  SDLKEY_ENTER = 1 << 6,
  SDLKEY_NAV = 1 << 7,
  SDLKEY_START = 1 << 8,
};

class sdlEventManager : public EventManager {
public:
  sdlEventManager();
  virtual ~sdlEventManager();

  virtual bool Init() override;
  virtual int MainLoop() override;
  virtual void PostQuitMessage() override;
  virtual int GetKeyCode(const char *name) override;

  // Headless/scripted runs: quit automatically after this many milliseconds,
  // optionally writing a screenshot first.
  static void SetExitAfterMs(uint32_t ms);
  static void SetScreenshotPath(const char *path);
  // Comma-separated key names replayed into the UI at startup, e.g.
  // "nav,down,down,enter". Lets the emulator be driven from a script.
  static void SetKeyScript(const char *script);

private:
  void processSDLEvents();
  void dispatchButtons();
  // Feeds the next --keys step, if any are pending.
  void advanceKeyScript(uint64_t nowMs);

  static bool finished_;
  static uint16_t buttonMask_;
  static bool isRepeating_;
  static unsigned long time_;
  static unsigned int keyRepeat_;
  static unsigned int keyDelay_;
  static unsigned int keyKill_;

  static uint32_t exitAfterMs_;
  static const char *screenshotPath_;
  static const char *keyScript_;
};

#endif
