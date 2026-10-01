/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

// Host stand-in for the device's record.h. RecordView links against this
// API. The pico build's implementation is itself an all-stub no-op (the
// original hardware has no line-in or mic), so the emulator matches it
// exactly rather than inventing capture the device doesn't have.

#ifndef _RECORD_SDL_H_
#define _RECORD_SDL_H_

#include <cstdint>

#define LINEIN_GAIN_MINDB 0
#define LINEIN_GAIN_MAXDB 0
#define MIC_GAIN_MINDB 0
#define MIC_GAIN_MAXDB 0

enum RecordSource { AllOff, LineIn, Mic, USBIn };

void Record(void *);
bool StartRecording(const char *filename, uint8_t threshold,
                    uint32_t milliseconds);
void StopRecording();
void RequestStopRecording();
bool WaitForRecordingStop(uint32_t timeoutMs);
void FinishStopRecording();
void StartMonitoring();
void StopMonitoring();
void SetInputSource(RecordSource source);
void SetLineInGain(uint8_t gainDb);
void SetMicGain(uint8_t gainDb);
bool IsRecordingActive();
bool IsSavingRecording();
uint8_t GetSavingProgressPercent();
bool DidLastRecordingCaptureAudio();

#endif
