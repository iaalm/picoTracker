/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlAudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include <string.h>

sdlAudioDriver::sdlAudioDriver(AudioSettings &settings)
    : AudioDriver(settings), stream_(NULL), volume_(100), startTime_(0) {}

sdlAudioDriver::~sdlAudioDriver() { CloseDriver(); }

bool sdlAudioDriver::InitDriver() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    Trace::Error("SDL: cannot init audio: %s", SDL_GetError());
    return false;
  }

  // Match the device exactly: 44.1kHz, 16-bit signed, stereo interleaved.
  SDL_AudioSpec spec;
  spec.freq = 44100;
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      AudioCallback, this);
  if (!stream_) {
    Trace::Error("SDL: cannot open audio device: %s", SDL_GetError());
    return false;
  }

  Trace::Log("SDLAUDIO", "opened 44100Hz S16 stereo");
  return true;
}

void sdlAudioDriver::CloseDriver() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = NULL;
  }
}

bool sdlAudioDriver::StartDriver() {
  if (!stream_) {
    return false;
  }
  startTime_ = SDL_GetTicks();
  isPlaying_ = true;
  return SDL_ResumeAudioStreamDevice(stream_);
}

void sdlAudioDriver::StopDriver() {
  isPlaying_ = false;
  if (stream_) {
    SDL_PauseAudioStreamDevice(stream_);
  }
}

void SDLCALL sdlAudioDriver::AudioCallback(void *userdata,
                                           SDL_AudioStream *stream,
                                           int additional_amount,
                                           int total_amount) {
  ((sdlAudioDriver *)userdata)->onBufferNeeded(stream, additional_amount);
}

void sdlAudioDriver::onBufferNeeded(SDL_AudioStream *stream,
                                    int additionalBytes) {
  if (!isPlaying_) {
    return;
  }

  // Mirror the device's chunk-done handler: service MIDI, then ask the mixer
  // to render. On the device the mixer runs on core1 and this runs in an IRQ;
  // here both happen on SDL's audio thread, so the render is synchronous and
  // the buffer is ready immediately after OnNewBufferNeeded() returns.
  MidiService::GetInstance()->Flush();

  while (additionalBytes > 0) {
    pool_[poolQueuePosition_].empty_ = true;
    OnNewBufferNeeded();

    AudioBufferData &buf = pool_[poolPlayPosition_];
    if (buf.empty_) {
      // Mixer produced nothing; feed silence rather than let SDL underrun.
      short silence[256] = {0};
      int chunk = (additionalBytes < (int)sizeof(silence))
                      ? additionalBytes
                      : (int)sizeof(silence);
      SDL_PutAudioStreamData(stream, silence, chunk);
      additionalBytes -= chunk;
      continue;
    }

    int size = buf.size_;
    if (volume_ != 100) {
      // The device scales in hardware; do it in software here.
      short *samples = (short *)buf.buffer_;
      int count = size / sizeof(short);
      for (int i = 0; i < count; i++) {
        samples[i] = (short)((samples[i] * volume_) / 100);
      }
    }

    SDL_PutAudioStreamData(stream, buf.buffer_, size);
    buf.empty_ = true;
    poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
    additionalBytes -= size;
  }
}

int sdlAudioDriver::GetPlayedBufferPercentage() { return 0; }

double sdlAudioDriver::GetStreamTime() {
  return (SDL_GetTicks() - startTime_) / 1000.0;
}

void sdlAudioDriver::SetVolume(int v) { volume_ = v; }

int sdlAudioDriver::GetVolume() { return volume_; }
