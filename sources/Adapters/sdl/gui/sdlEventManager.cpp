/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlEventManager.h"
#include "Adapters/sdl/display/sdlchargfx.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "sdlGUIWindowImp.h"
#include <SDL3/SDL.h>

bool sdlEventManager::finished_ = false;
uint16_t sdlEventManager::buttonMask_ = 0;
bool sdlEventManager::isRepeating_ = false;
unsigned long sdlEventManager::time_ = 0;
unsigned int sdlEventManager::keyRepeat_ = 25;
unsigned int sdlEventManager::keyDelay_ = 500;
unsigned int sdlEventManager::keyKill_ = 5;
uint32_t sdlEventManager::exitAfterMs_ = 0;
const char *sdlEventManager::screenshotPath_ = NULL;
const char *sdlEventManager::keyScript_ = NULL;

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
      buttonMask_ |= scancodeToKeyMask(event.key.scancode);
      break;
    }
    case SDL_EVENT_KEY_UP:
      buttonMask_ &= ~scancodeToKeyMask(event.key.scancode);
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
    buttonMask_ &= ~currentMask;
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
    buttonMask_ |= currentMask;
    pressed = true;
  }
  cursor = comma ? comma + 1 : cursor + len;
  nextStepMs = nowMs + KEY_STEP_MS;
}

int sdlEventManager::MainLoop() {
  uint64_t startMs = SDL_GetTicks();
  uint64_t lastClockMs = startMs;
  uint16_t lastMask = 0;

  while (!finished_) {
    processSDLEvents();
    advanceKeyScript(SDL_GetTicks());

    // Dispatch the diff against the previous frame. There's no bounce on a
    // host keyboard, so unlike the device there is nothing to debounce --
    // but we keep the auto-repeat behaviour the UI expects.
    unsigned long now = System::GetInstance()->GetClock();
    uint16_t sendMask =
        (buttonMask_ ^ lastMask) |
        (buttonMask_ & (SDLKEY_LEFT | SDLKEY_RIGHT | SDLKEY_UP | SDLKEY_DOWN));

    bool gotEvent = false;
    if (buttonMask_ == lastMask) {
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
      sdlGUIWindowImp::ProcessButtonChange(sendMask, buttonMask_);
      lastMask = buttonMask_;
    }

    // Drive the UI clock at the same ~30Hz the device uses.
    uint64_t nowMs = SDL_GetTicks();
    if ((nowMs - lastClockMs) >= PICO_CLOCK_INTERVAL) {
      lastClockMs = nowMs;
      sdlGUIWindowImp::ProcessClockTick();
      sdlGUIWindowImp::ProcessFlush();
    }

    if (exitAfterMs_ > 0 && (nowMs - startMs) >= exitAfterMs_) {
      if (screenshotPath_) {
        sdlchargfx_present();
        if (sdlchargfx_screenshot(screenshotPath_)) {
          Trace::Log("SDL", "wrote screenshot to %s", screenshotPath_);
        }
      }
      finished_ = true;
    }

    SDL_Delay(1);
  }
  return 0;
}

void sdlEventManager::PostQuitMessage() { finished_ = true; }

int sdlEventManager::GetKeyCode(const char *name) { return -1; }
