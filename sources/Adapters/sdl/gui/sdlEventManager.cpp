/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlEventManager.h"
#ifdef __EMSCRIPTEN__
// Must precede the project headers: TinyXML2/tinyxml2adapter.h (pulled in
// transitively by Config.h) does `#define FILE I_File` plus a batch of stdio
// macros, which would rewrite emscripten.h's own declarations.
#include <emscripten.h>
#endif
#include "Adapters/sdl/display/sdlchargfx.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "sdlGUIWindowImp.h"
#include <SDL3/SDL.h>

bool sdlEventManager::finished_ = false;
uint16_t sdlEventManager::keyMask_ = 0;
bool sdlEventManager::isRepeating_ = false;
unsigned long sdlEventManager::time_ = 0;
unsigned int sdlEventManager::keyRepeat_ = 25;
unsigned int sdlEventManager::keyDelay_ = 500;
unsigned int sdlEventManager::keyKill_ = 5;
uint32_t sdlEventManager::exitAfterMs_ = 0;
const char *sdlEventManager::screenshotPath_ = NULL;
const char *sdlEventManager::keyScript_ = NULL;
uint64_t sdlEventManager::startMs_ = 0;
uint64_t sdlEventManager::lastClockMs_ = 0;
uint16_t sdlEventManager::lastMask_ = 0;
uint16_t sdlEventManager::touchMask_ = 0;

#ifdef __EMSCRIPTEN__
// Called from the shell page's on-screen pad. The device keypad is a
// bitmask and the UI relies on combos (ALT+arrow, NAV+arrow), so the pad
// sends the full set of currently-held buttons rather than press/release
// pairs: one assignment can't desync, and multi-touch falls out for free.
extern "C" EMSCRIPTEN_KEEPALIVE void pt_set_touch_mask(int mask) {
  sdlEventManager::SetTouchMask((uint16_t)mask);
}
#endif

void sdlEventManager::SetTouchMask(uint16_t mask) { touchMask_ = mask; }

void sdlEventManager::SetKeyScript(const char *script) { keyScript_ = script; }

sdlEventManager::sdlEventManager() {}
sdlEventManager::~sdlEventManager() {}

void sdlEventManager::SetExitAfterMs(uint32_t ms) { exitAfterMs_ = ms; }

void sdlEventManager::SetScreenshotPath(const char *path) {
  screenshotPath_ = path;
}

bool sdlEventManager::Init() {
  EventManager::Init();
  return true;
}

// Maps a host keyboard scancode onto the device's keypad bit, or 0 when the
// key isn't bound.
static uint16_t scancodeToKeyMask(SDL_Scancode sc) {
  switch (sc) {
  case SDL_SCANCODE_LEFT:
    return SDLKEY_LEFT;
  case SDL_SCANCODE_RIGHT:
    return SDLKEY_RIGHT;
  case SDL_SCANCODE_UP:
    return SDLKEY_UP;
  case SDL_SCANCODE_DOWN:
    return SDLKEY_DOWN;
  // A/S match the device's two "action" switches; Z/X are offered as
  // alternates for keyboards where A/S collide with window shortcuts.
  case SDL_SCANCODE_A:
  case SDL_SCANCODE_Z:
    return SDLKEY_ENTER;
  case SDL_SCANCODE_S:
  case SDL_SCANCODE_X:
    return SDLKEY_EDIT;
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT:
    return SDLKEY_ALT;
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:
    return SDLKEY_NAV;
  case SDL_SCANCODE_SPACE:
    return SDLKEY_START;
  default:
    return 0;
  }
}

void sdlEventManager::processSDLEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      finished_ = true;
      break;
    case SDL_EVENT_KEY_DOWN: {
      if (event.key.repeat) {
        // The firmware does its own key repeat; ignore the host's.
        break;
      }
      if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
        finished_ = true;
        break;
      }
      keyMask_ |= scancodeToKeyMask(event.key.scancode);
      break;
    }
    case SDL_EVENT_KEY_UP:
      keyMask_ &= ~scancodeToKeyMask(event.key.scancode);
      break;
    default:
      break;
    }
  }
}

// Maps a --keys script token onto a keypad bit.
static uint16_t keyNameToMask(const char *name, size_t len) {
  struct {
    const char *name;
    uint16_t mask;
  } table[] = {
      {"left", SDLKEY_LEFT},   {"right", SDLKEY_RIGHT}, {"up", SDLKEY_UP},
      {"down", SDLKEY_DOWN},   {"alt", SDLKEY_ALT},     {"edit", SDLKEY_EDIT},
      {"enter", SDLKEY_ENTER}, {"nav", SDLKEY_NAV},     {"play", SDLKEY_START},
  };
  for (auto &e : table) {
    if (strlen(e.name) == len && strncmp(e.name, name, len) == 0) {
      return e.mask;
    }
  }
  return 0;
}

// Replays the --keys script: each step presses a key for one interval, then
// releases it for one, so the UI sees clean press/release pairs.
void sdlEventManager::advanceKeyScript(uint64_t nowMs) {
  static const char *cursor = NULL;
  static uint64_t nextStepMs = 0;
  static bool pressed = false;
  static uint16_t currentMask = 0;
  static bool started = false;

  if (!keyScript_) {
    return;
  }
  if (!started) {
    cursor = keyScript_;
    // Let the UI settle before the first synthetic key.
    nextStepMs = nowMs + 300;
    started = true;
  }
  if (nowMs < nextStepMs) {
    return;
  }

  // Each key is held for KEY_STEP_MS then released for the same, which is
  // comfortably longer than the UI's key-kill threshold.
  const uint64_t KEY_STEP_MS = 120;

  if (pressed) {
    keyMask_ &= ~currentMask;
    pressed = false;
    nextStepMs = nowMs + KEY_STEP_MS;
    return;
  }

  if (!cursor || *cursor == '\0') {
    return;
  }
  const char *comma = strchr(cursor, ',');
  size_t len = comma ? (size_t)(comma - cursor) : strlen(cursor);
  currentMask = keyNameToMask(cursor, len);
  if (currentMask == 0) {
    Trace::Error("SDL: unknown key in --keys: %.*s", (int)len, cursor);
  } else {
    keyMask_ |= currentMask;
    pressed = true;
  }
  cursor = comma ? comma + 1 : cursor + len;
  nextStepMs = nowMs + KEY_STEP_MS;
}

void sdlEventManager::RunOneFrame() {
  processSDLEvents();
  advanceKeyScript(SDL_GetTicks());

  // Keyboard and on-screen pad are tracked separately and OR'd together, so
  // releasing a key can't clear a button the pad is still holding (and vice
  // versa).
  const uint16_t buttonMask = keyMask_ | touchMask_;

  // Dispatch the diff against the previous frame. There's no bounce on a
  // host keyboard, so unlike the device there is nothing to debounce --
  // but we keep the auto-repeat behaviour the UI expects.
  unsigned long now = System::GetInstance()->GetClock();
  uint16_t sendMask =
      (buttonMask ^ lastMask_) |
      (buttonMask & (SDLKEY_LEFT | SDLKEY_RIGHT | SDLKEY_UP | SDLKEY_DOWN));

  bool gotEvent = false;
  if (buttonMask == lastMask_) {
    if (isRepeating_ && ((now - time_) > keyRepeat_)) {
      gotEvent = (sendMask != 0);
    }
    if (!isRepeating_ && ((now - time_) > keyDelay_)) {
      gotEvent = (sendMask != 0);
      if (gotEvent) {
        isRepeating_ = true;
      }
    }
  } else {
    if ((now - time_) > keyKill_) {
      gotEvent = (sendMask != 0);
      if (gotEvent) {
        isRepeating_ = false;
      }
    }
  }

  if (gotEvent) {
    time_ = now;
    sdlGUIWindowImp::ProcessButtonChange(sendMask, buttonMask);
    lastMask_ = buttonMask;
  }

  // Drive the UI clock at the same ~30Hz the device uses.
  uint64_t nowMs = SDL_GetTicks();
  if ((nowMs - lastClockMs_) >= PICO_CLOCK_INTERVAL) {
    lastClockMs_ = nowMs;
    sdlGUIWindowImp::ProcessClockTick();
    sdlGUIWindowImp::ProcessFlush();
  }

  if (exitAfterMs_ > 0 && (nowMs - startMs_) >= exitAfterMs_) {
    if (screenshotPath_) {
      sdlchargfx_present();
      if (sdlchargfx_screenshot(screenshotPath_)) {
        Trace::Log("SDL", "wrote screenshot to %s", screenshotPath_);
      }
    }
    finished_ = true;
  }
}

#ifdef __EMSCRIPTEN__
// The browser owns the event loop: a C function that never returns would
// starve rendering, input and audio, since all three are serviced by the
// same JS task queue. So instead of blocking we hand one frame at a time
// back to the browser and let it call us at display refresh rate.
static void emFrame(void *arg) {
  sdlEventManager *self = (sdlEventManager *)arg;
  if (self->IsFinished()) {
    emscripten_cancel_main_loop();
    return;
  }
  self->RunOneFrame();
}
#endif

int sdlEventManager::MainLoop() {
  startMs_ = SDL_GetTicks();
  lastClockMs_ = startMs_;
  lastMask_ = 0;

#ifdef __EMSCRIPTEN__
  // fps=0 means requestAnimationFrame; simulate_infinite_loop=1 unwinds the
  // C stack here and returns control to the browser, so nothing after this
  // call runs (shutdown happens on the quit path instead).
  emscripten_set_main_loop_arg(emFrame, this, 0, 1);
#else
  while (!finished_) {
    RunOneFrame();
    SDL_Delay(1);
  }
#endif
  return 0;
}

void sdlEventManager::PostQuitMessage() { finished_ = true; }

int sdlEventManager::GetKeyCode(const char *name) { return -1; }
