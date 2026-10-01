/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Adapters/sdl/display/sdlchargfx.h"
#include "Adapters/sdl/gui/sdlEventManager.h"
#include "Adapters/sdl/system/sdlSystem.h"
#include "Application/Application.h"
#include "System/Console/Trace.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
  printf("picoTracker emulator\n\n");
  printf("Usage: %s [options]\n\n", prog);
  printf("  --sdroot <dir>       directory to use as the SD card root\n");
  printf("                       (default: ./test_root)\n");
  printf("  --scale <n>          window scale factor (default: 2)\n");
  printf("  --exit-after <ms>    quit automatically after n milliseconds\n");
  printf("  --screenshot <path>  write a BMP before exiting (with "
         "--exit-after)\n");
  printf("  --keys <list>        replay keys on startup, comma separated\n");
  printf("                       (left,right,up,down,alt,edit,enter,nav,"
         "play)\n");
  printf("  --help               show this message\n\n");
  printf("Keys: arrows=dpad  A/Z=ENTER  S/X=EDIT  shift=ALT  ctrl=NAV\n");
  printf("      space=PLAY  esc=quit\n");
}

int main(int argc, char *argv[]) {
  const char *sdRoot = "test_root";
  int scale = 2;
  uint32_t exitAfterMs = 0;
  const char *screenshotPath = NULL;
  const char *keyScript = NULL;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      usage(argv[0]);
      return 0;
    } else if (!strcmp(argv[i], "--sdroot") && i + 1 < argc) {
      sdRoot = argv[++i];
    } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
      scale = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--exit-after") && i + 1 < argc) {
      exitAfterMs = (uint32_t)atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
      keyScript = argv[++i];
    } else {
      fprintf(stderr, "unknown option: %s\n\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
  }

  if (!SDL_Init(0)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  if (!sdlchargfx_init("picoTracker", scale)) {
    fprintf(stderr, "failed to open window\n");
    return 1;
  }

  Trace::RegisterEtlErrorHandler();

  sdlSystem::sdRoot_ = sdRoot;
  sdlSystem::Boot(argc, argv);

  sdlEventManager::SetExitAfterMs(exitAfterMs);
  sdlEventManager::SetScreenshotPath(screenshotPath);
  sdlEventManager::SetKeyScript(keyScript);

  GUICreateWindowParams params;
  params.title = "picoTracker";
  Application::GetInstance()->Init(params);

  int rc = sdlSystem::MainLoop();

  sdlSystem::Shutdown();
  return rc;
}
