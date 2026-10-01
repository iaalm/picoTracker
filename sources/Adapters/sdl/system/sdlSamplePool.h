/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_SAMPLE_POOL_H_
#define _SDL_SAMPLE_POOL_H_

#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/WavFile.h"
#include "System/Console/Trace.h"
#include <stdint.h>

// On the device, samples are programmed into flash and played straight out of
// the XIP-mapped window. There is no flash here, so the emulator keeps one
// large RAM arena and hands out slices of it. The arena is deliberately sized
// to the device's 8MB sample budget so that "sample won't fit" behaves the
// same way it does on hardware -- an emulator that lets you load samples the
// device could never hold would hide real bugs.
#define SDL_SAMPLE_ARENA_SIZE (8 * 1024 * 1024)

class sdlSamplePool : public SamplePool {
public:
  sdlSamplePool();
  virtual ~sdlSamplePool() {}

  virtual void Reset() override;
  virtual bool CheckSampleFits(int sampleSize) override;
  virtual uint32_t GetAvailableSampleStorageSpace() override {
    return SDL_SAMPLE_ARENA_SIZE - writeOffset_;
  }

protected:
  virtual bool loadSample(const char *name) override;
  virtual bool unloadSample(uint32_t index) override;

private:
  bool loadInArena(WavFile *wave);

  static uint8_t arena_[SDL_SAMPLE_ARENA_SIZE];
  static uint32_t writeOffset_;
};

#endif
