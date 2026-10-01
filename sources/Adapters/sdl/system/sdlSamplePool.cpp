/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlSamplePool.h"
#include <cstring>

// Page size the device rounds sample allocations up to. Kept here so the
// emulator's "does it fit" answer matches the hardware's.
#define SDL_SAMPLE_PAGE_SIZE 256

uint8_t sdlSamplePool::arena_[SDL_SAMPLE_ARENA_SIZE];
uint32_t sdlSamplePool::writeOffset_ = 0;

sdlSamplePool::sdlSamplePool() : SamplePool() {
  Trace::Log("SAMPLEPOOL", "RAM arena: %d bytes", SDL_SAMPLE_ARENA_SIZE);
}

void sdlSamplePool::Reset() {
  count_ = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    wav_[i].Close();
    nameStore_[i][0] = '\0';
  }
  writeOffset_ = 0;
}

bool sdlSamplePool::CheckSampleFits(int sampleSize) {
  uint32_t needed = ((sampleSize / SDL_SAMPLE_PAGE_SIZE) +
                     ((sampleSize % SDL_SAMPLE_PAGE_SIZE) != 0)) *
                    SDL_SAMPLE_PAGE_SIZE;
  return needed <= GetAvailableSampleStorageSpace();
}

bool sdlSamplePool::loadSample(const char *name) {
  Trace::Log("SAMPLEPOOL", "Loading sample into RAM: %s", name);

  if (count_ == MAX_SAMPLES) {
    return false;
  }

  auto res = wav_[count_].Open(name);
  if (!res) {
    Trace::Error("Failed to load sample:%s", name);
    return false;
  }
  strncpy(nameStore_[count_], name, MAX_INSTRUMENT_FILENAME_LENGTH);
  nameStore_[count_][MAX_INSTRUMENT_FILENAME_LENGTH] = '\0';
  count_++;

  updateStatus(importIndex, importCount, "Importing");

  if (!loadInArena(&wav_[count_ - 1])) {
    Trace::Error("Failed to load sample into RAM: %s", name);
    count_--;
    nameStore_[count_][0] = '\0';
    wav_[count_].Close();
    return false;
  }

  wav_[count_ - 1].Close();
  return true;
}

bool sdlSamplePool::loadInArena(WavFile *wave) {
  uint32_t diskSize = wave->GetDiskSize(-1);
  uint32_t pagedSize = ((diskSize / SDL_SAMPLE_PAGE_SIZE) +
                        ((diskSize % SDL_SAMPLE_PAGE_SIZE) != 0)) *
                       SDL_SAMPLE_PAGE_SIZE;

  if (writeOffset_ + pagedSize > SDL_SAMPLE_ARENA_SIZE) {
    return false;
  }

  // The sample buffer points into the arena, the same way the device points
  // it into the XIP flash window.
  wave->SetSampleBuffer((short *)(arena_ + writeOffset_));

  uint32_t written = 0;
  uint32_t br = 0;
  uint8_t readBuffer[BUFFER_SIZE];

  wave->Rewind();
  wave->Read(&readBuffer, BUFFER_SIZE, &br);
  while (br > 0) {
    if (written + br > pagedSize) {
      Trace::Error("SAMPLEPOOL: sample longer than its reported size");
      return false;
    }
    memcpy(arena_ + writeOffset_ + written, readBuffer, br);
    written += br;
    wave->Read(&readBuffer, BUFFER_SIZE, &br);
  }

  // Zero the tail of the final page so playback past the last sample reads
  // silence rather than whatever the previous project left behind.
  if (written < pagedSize) {
    memset(arena_ + writeOffset_ + written, 0, pagedSize - written);
  }

  writeOffset_ += pagedSize;
  return true;
}

// Matches the device: samples are only reclaimed by a full Reset().
bool sdlSamplePool::unloadSample(uint32_t index) { return false; }
