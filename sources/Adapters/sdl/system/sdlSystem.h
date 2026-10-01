/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_SYSTEM_H_
#define _SDL_SYSTEM_H_

#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

class sdlSystem : public System {
public:
  static void Boot(int argc, char **argv);
  static void Shutdown();
  static int MainLoop();

  virtual unsigned long GetClock() override;
  virtual void GetBatteryState(BatteryState &state) override;
  virtual void SetDisplayBrightness(unsigned char value) override;
  virtual void PostQuitMessage() override;
  virtual unsigned int GetMemoryUsage() override;
  virtual void PowerDown() override;
  virtual void SystemPutChar(int c) override;
  virtual void SystemBootloader() override;
  virtual void SystemReboot() override;
  virtual uint32_t GetRandomNumber() override;
  virtual uint32_t Micros() override;
  virtual uint32_t Millis() override;

  // Path the emulator treats as the SD card root.
  static const char *sdRoot_;

private:
  static EventManager *eventManager_;
};

#endif
