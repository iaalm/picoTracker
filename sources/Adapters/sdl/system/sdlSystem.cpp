/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlSystem.h"
#include "Adapters/sdl/audio/sdlAudio.h"
#include "Adapters/sdl/display/sdlchargfx.h"
#include "Adapters/sdl/filesystem/sdlFileSystem.h"
#include "Adapters/sdl/gui/sdlGUIFactory.h"
#include "Adapters/sdl/midi/sdlMidiService.h"
#include "Adapters/sdl/system/sdlSamplePool.h"
#include "Adapters/sdl/timer/sdlTimer.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

EventManager *sdlSystem::eventManager_ = NULL;
const char *sdlSystem::sdRoot_ = ".";

int sdlSystem::MainLoop() {
  eventManager_->InstallMappings();
  return eventManager_->MainLoop();
}

void sdlSystem::Boot(int argc, char **argv) {
  // Install order mirrors the device adapter; in particular MIDI must be
  // installed before Audio because installing Audio triggers the config read,
  // which applies MIDI settings.
  alignas(sdlSystem) static char systemMemBuf[sizeof(sdlSystem)];
  System::Install(new (systemMemBuf) sdlSystem());

  alignas(sdlGUIFactory) static char guiMemBuf[sizeof(sdlGUIFactory)];
  I_GUIWindowFactory::Install(new (guiMemBuf) sdlGUIFactory());

  alignas(sdlTimerService) static char timerMemBuf[sizeof(sdlTimerService)];
  TimerService::GetInstance()->Install(new (timerMemBuf) sdlTimerService());

  alignas(sdlFileSystem) static char fsMemBuf[sizeof(sdlFileSystem)];
  FileSystem::Install(new (fsMemBuf) sdlFileSystem(sdRoot_));

  auto fs = FileSystem::GetInstance();
  if (!fs->chdir("/")) {
    Trace::Error("SDL: cannot enter SD root %s", sdRoot_);
    exit(1);
  }

  alignas(sdlMidiService) static char midiMemBuf[sizeof(sdlMidiService)];
  MidiService::Install(new (midiMemBuf) sdlMidiService());

  AudioSettings hint;
  hint.bufferSize_ = 1024;
  hint.preBufferCount_ = 8;
  alignas(sdlAudio) static char audioMemBuf[sizeof(sdlAudio)];
  Audio::Install(new (audioMemBuf) sdlAudio(hint));

  alignas(sdlSamplePool) static char samplePoolMemBuf[sizeof(sdlSamplePool)];
  SamplePool::Install(new (samplePoolMemBuf) sdlSamplePool());

  eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
  eventManager_->Init();
}

void sdlSystem::Shutdown() {
  sdlchargfx_shutdown();
  SDL_Quit();
}

unsigned long sdlSystem::GetClock() { return SDL_GetTicks(); }

uint32_t sdlSystem::Millis() { return (uint32_t)SDL_GetTicks(); }

uint32_t sdlSystem::Micros() { return (uint32_t)(SDL_GetTicksNS() / 1000); }

uint32_t sdlSystem::GetRandomNumber() { return SDL_rand_bits(); }

// No battery on a desktop: report a healthy, non-charging pack so the UI has
// something sane to draw rather than an error state.
void sdlSystem::GetBatteryState(BatteryState &state) {
  state.percentage = 100;
  state.voltage_mv = 4200;
  state.temperature_c = 25;
  state.charging = false;
  state.error = false;
}

void sdlSystem::SetDisplayBrightness(unsigned char value) {}

void sdlSystem::PostQuitMessage() {
  if (eventManager_) {
    eventManager_->PostQuitMessage();
  }
}

// The device reports real heap headroom here; there is no meaningful
// equivalent on a host, and reporting a fake number would misrepresent the
// memory pressure that matters on hardware.
unsigned int sdlSystem::GetMemoryUsage() { return 0; }

void sdlSystem::PowerDown() {
  if (eventManager_) {
    eventManager_->PostQuitMessage();
  }
}

void sdlSystem::SystemPutChar(int c) { putchar(c); }

void sdlSystem::SystemBootloader() {
  Trace::Log("SDL", "bootloader requested (no-op on host)");
}

void sdlSystem::SystemReboot() {
  Trace::Log("SDL", "reboot requested, quitting");
  if (eventManager_) {
    eventManager_->PostQuitMessage();
  }
}
