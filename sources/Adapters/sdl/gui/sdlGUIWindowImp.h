/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_GUI_WINDOW_IMP_H_
#define _SDL_GUI_WINDOW_IMP_H_

#include "Adapters/sdl/display/sdlchargfx.h"
#include "Foundation/Observable.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include <stdint.h>

class sdlGUIWindowImp : public I_GUIWindowImp, public I_Observer {
public:
  sdlGUIWindowImp(GUICreateWindowParams &p);
  virtual ~sdlGUIWindowImp();

public: // I_GUIWindowImp implementation
  virtual void SetColor(GUIColor &) override;
  virtual void DrawRect(GUIRect &) override;
  virtual void DrawChar(const char c, const GUIPoint &pos,
                        const GUITextProperties &props) override;
  virtual void DrawString(const char *string, const GUIPoint &pos,
                          const GUITextProperties &props,
                          bool overlay = false) override{};
  virtual GUIRect GetRect() override;
  virtual void Invalidate() override;
  virtual void Flush() override;
  virtual void Lock() override;
  virtual void Unlock() override;
  virtual void Clear(GUIColor &, bool overlay = false) override;
  virtual void ClearTextRect(GUIRect &) override;
  virtual void PushEvent(GUIEvent &event) override;

  // Called from the event loop to drive the framework the same way the
  // device's event queue does.
  static void ProcessRedraw();
  static void ProcessFlush();
  static void ProcessClockTick();
  static void ProcessButtonChange(uint16_t changeMask, uint16_t buttonMask);

  static sdlGUIWindowImp *instance_;

protected:
  static sdlchargfx_color_t GetColor(GUIColor &c);
  virtual void Update(Observable &o, I_ObservableData *d) override;
};

#endif
