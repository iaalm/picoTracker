/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlGUIWindowImp.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/RemoteUI/RemoteUIProtocol.h"
#include "System/System/System.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

// The emulator presents the same button layout as the device, so it reuses
// the device's pad mapping verbatim.
static GUIEventPadButtonType *eventMapping = eventMappingPico;

// Tracks the palette entries already pushed down, mirroring the device so we
// only touch the palette when a colour actually changes.
static uint16_t lastPaletteRGB[16] = {0};
static uint8_t lastColorIdx = 255;

sdlGUIWindowImp *sdlGUIWindowImp::instance_ = NULL;

sdlGUIWindowImp::sdlGUIWindowImp(GUICreateWindowParams &p) {
  instance_ = this;

  Config *config = Config::GetInstance();
  auto uiFontVar = (WatchedVariable *)config->FindVariable(FourCC::VarUIFont);
  uiFontVar->AddObserver(*this);
  sdlchargfx_set_font_index(uiFontVar->GetInt());
}

sdlGUIWindowImp::~sdlGUIWindowImp() {}

sdlchargfx_color_t sdlGUIWindowImp::GetColor(GUIColor &c) {
  if (c._paletteIndex >= 16) {
    return 1; // CHARGFX_NORMAL equivalent
  }
  uint16_t rgb565 = to_rgb565(c);
  if (lastPaletteRGB[c._paletteIndex] != rgb565) {
    sdlchargfx_set_palette_color(c._paletteIndex, rgb565);
    lastPaletteRGB[c._paletteIndex] = rgb565;
  }
  return (sdlchargfx_color_t)c._paletteIndex;
}

void sdlGUIWindowImp::SetColor(GUIColor &c) {
  sdlchargfx_color_t color = GetColor(c);
  lastColorIdx = color;
  sdlchargfx_set_foreground(color);
}

void sdlGUIWindowImp::DrawChar(const char c, const GUIPoint &pos,
                               const GUITextProperties &p) {
  // The framework addresses the screen in 8px units; the display is a grid of
  // character cells, matching the device's conversion.
  sdlchargfx_set_cursor(pos._x / 8, pos._y / 8);
  sdlchargfx_putc(c, p.invert_);
}

void sdlGUIWindowImp::DrawRect(GUIRect &r) {
  sdlchargfx_fill_rect(lastColorIdx, r.Left(), r.Top(), r.Width(), r.Height());
}

void sdlGUIWindowImp::Clear(GUIColor &c, bool overlay) {
  sdlchargfx_color_t background = GetColor(c);
  sdlchargfx_set_background(background);
  sdlchargfx_clear(background);
}

void sdlGUIWindowImp::ClearTextRect(GUIRect &r) {}

void sdlGUIWindowImp::Lock() {}

void sdlGUIWindowImp::Unlock() {}

void sdlGUIWindowImp::Flush() { sdlchargfx_present(); }

void sdlGUIWindowImp::Invalidate() {
  // On the device this queues a flush for the event loop. Here the loop
  // flushes every frame, so repainting the window is enough.
  if (instance_ && instance_->_window) {
    instance_->_window->Update(false);
  }
  sdlchargfx_present();
}

void sdlGUIWindowImp::PushEvent(GUIEvent &event) {}

GUIRect sdlGUIWindowImp::GetRect() {
  return GUIRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void sdlGUIWindowImp::ProcessRedraw() {
  if (instance_ && instance_->_window) {
    instance_->_window->Update(true);
  }
}

void sdlGUIWindowImp::ProcessFlush() {
  if (instance_ && instance_->_window) {
    instance_->_window->Update(false);
  }
}

void sdlGUIWindowImp::ProcessClockTick() {
  if (instance_ && instance_->_window) {
    instance_->_window->ClockTick();
  }
}

void sdlGUIWindowImp::ProcessButtonChange(uint16_t changeMask,
                                          uint16_t buttonMask) {
  int e = 1;
  unsigned long now = System::GetInstance()->GetClock();
  for (int i = 0; i < 10; i++) {
    if (changeMask & e) {
      GUIEventType type = (buttonMask & e) ? ET_PADBUTTONDOWN : ET_PADBUTTONUP;
      GUIEvent event(eventMapping[i], type, now, 0, 0, 0);
      instance_->_window->DispatchEvent(event);
    }
    e = e << 1;
  }
}

void sdlGUIWindowImp::Update(Observable &o, I_ObservableData *d) {
  WatchedVariable &v = (WatchedVariable &)o;
  switch (v.GetID()) {
  case FourCC::VarUIFont:
    sdlchargfx_set_font_index(v.GetInt());
    break;
  default:
    break;
  }
}
