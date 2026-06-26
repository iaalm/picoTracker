/*
 * Host-only WAV writer for synth_render (no FileSystem HAL).
 */
#pragma once

#include "Application/Instruments/WavHeader.h"
#include "Application/Utils/fixed.h"
#include "System/FileSystem/I_File.h"

#include <cstdio>
#include <vector>

class HostFile final : public I_File {
public:
  HostFile() = default;

  explicit HostFile(const char *path) { Open(path); }

  bool Open(const char *path) {
    Close();
    file_ = std::fopen(path, "wb");
    return file_ != nullptr;
  }

  bool IsOpen() const { return file_ != nullptr; }

  int Read(void *ptr, int size) override {
    if (!file_ || size <= 0) {
      return 0;
    }
    return static_cast<int>(std::fread(ptr, 1, static_cast<size_t>(size), file_));
  }

  int GetC() override {
    if (!file_) {
      error_ = true;
      return -1;
    }
    int c = std::fgetc(file_);
    if (c == EOF) {
      error_ = true;
      return -1;
    }
    return c;
  }

  int Write(const void *ptr, int size, int nmemb) override {
    if (!file_) {
      error_ = true;
      return 0;
    }
    size_t written =
        std::fwrite(ptr, static_cast<size_t>(size), static_cast<size_t>(nmemb), file_);
    if (written != static_cast<size_t>(nmemb)) {
      error_ = true;
    }
    return static_cast<int>(written);
  }

  void Seek(long offset, int whence) override {
    if (!file_) {
      error_ = true;
      return;
    }
    if (std::fseek(file_, offset, whence) != 0) {
      error_ = true;
    }
  }

  long Tell() override {
    if (!file_) {
      error_ = true;
      return -1;
    }
    return std::ftell(file_);
  }

  int Error() override { return error_ ? 1 : 0; }

  bool Sync() override {
    if (!file_) {
      return false;
    }
    return std::fflush(file_) == 0;
  }

  void Dispose() override { Close(); }

  void CloseFile() { Close(); }

protected:
  bool Close() override {
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
    return true;
  }

private:
  FILE *file_ = nullptr;
  bool error_ = false;
};

class HostWavWriter {
public:
  static constexpr uint32_t kSampleRate = 44100;
  static constexpr uint16_t kChannels = 2;
  static constexpr uint16_t kBytesPerSample = 2;

  bool Open(const char *path) {
    Close();
    sample_count_ = 0;
    if (!file_.Open(path)) {
      return false;
    }
    if (!WavHeaderWriter::WriteHeader(&file_, kSampleRate, kChannels,
                                      kBytesPerSample)) {
      file_.CloseFile();
      return false;
    }
    pcm_scratch_.resize(4096);
    return true;
  }

  void AddFixedBuffer(const fixed *interleaved, int frames) {
    if (!file_.IsOpen() || frames <= 0) {
      return;
    }

    const size_t samples = static_cast<size_t>(frames) * kChannels;
    if (pcm_scratch_.size() < samples) {
      pcm_scratch_.resize(samples);
    }

    const fixed f_max = i2fp(32767);
    const fixed f_min = i2fp(-32768);
    for (size_t i = 0; i < samples; ++i) {
      fixed v = interleaved[i];
      if (v > f_max) {
        v = f_max;
      } else if (v < f_min) {
        v = f_min;
      }
      pcm_scratch_[i] = static_cast<int16_t>(fp2i(v));
    }

    file_.Write(pcm_scratch_.data(), static_cast<int>(sizeof(int16_t)),
                static_cast<int>(samples));
    sample_count_ += static_cast<uint32_t>(frames);
  }

  bool Finalize() {
    if (!file_.IsOpen()) {
      return false;
    }
    const bool ok = WavHeaderWriter::UpdateFileSize(
        &file_, sample_count_, kChannels, kBytesPerSample);
    file_.CloseFile();
    return ok;
  }

  void Close() {
    if (file_.IsOpen()) {
      file_.CloseFile();
    }
    sample_count_ = 0;
  }

  uint32_t sample_count() const { return sample_count_; }

private:
  HostFile file_;
  uint32_t sample_count_ = 0;
  std::vector<int16_t> pcm_scratch_;
};
