/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _MEMORY_FILE_H_
#define _MEMORY_FILE_H_

#include "System/FileSystem/I_File.h"
#include <cstdio>
#include <cstring>
#include <stddef.h>
#include <stdint.h>

// A trivial I_File backed by an in-memory byte buffer. Used by
// PersistencyDocument::LoadFromBuffer so the existing iteration methods
// (FirstChild/NextSibling/NextAttribute/HasContent) can drive their
// yxml state machine through fp_->GetC() unchanged.
//
// MemoryFile is ONLY constructed on the buffer path; the production
// Load(filename) path uses fs->Open(...) which yields a FileHandle
// backed by the real FS, never MemoryFile. So the vtable cost is paid
// only on the host test side and on the SaveToBuffer -> LoadFromBuffer
// round-trip — not on the RP2040 hot path.
//
// Storage model: MemoryFile is always embedded as a value member of
// PersistencyDocument (or another stack/static owner). Dispose() is a
// no-op so FileHandle::reset() does not try to free the instance.
// This keeps the RP2040 build heap-free — the firmware disables
// `operator new`.
class MemoryFile : public I_File {
public:
  MemoryFile() : data_(nullptr), len_(0), pos_(0), error_(false) {}

  // Init from a buffer pointer + length. Resets pos_/error_ so the
  // MemoryFile can be reused across multiple LoadFromBuffer calls on
  // the same document.
  void Init(const uint8_t *data, size_t len) {
    data_ = data;
    len_ = len;
    pos_ = 0;
    error_ = false;
  }

  virtual ~MemoryFile() override = default;

  // Sequential byte read used by PersistencyDocument iteration methods.
  virtual int GetC() override {
    if (!data_ || pos_ >= len_) {
      return EOF;
    }
    return data_[pos_++];
  }

  // Raw byte read; matches the contract of picoTrackerFile::Read
  // (returns actual byte count read, may be < size at EOF).
  virtual int Read(void *ptr, int size) override {
    if (!data_ || !ptr || size <= 0) {
      error_ = true;
      return 0;
    }
    size_t remaining = len_ - pos_;
    size_t to_copy = remaining < (size_t)size ? remaining : (size_t)size;
    if (to_copy > 0) {
      memcpy(ptr, data_ + pos_, to_copy);
      pos_ += to_copy;
    }
    return (int)to_copy;
  }

  virtual int Write(const void * /*ptr*/, int /*size*/, int /*nmemb*/) override {
    error_ = true;
    return 0;
  }

  virtual void Seek(long offset, int whence) override {
    size_t base;
    switch (whence) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = pos_;
      break;
    case SEEK_END:
      base = len_;
      break;
    default:
      error_ = true;
      return;
    }
    // Compute the candidate target in signed arithmetic, then clamp.
    long long target = (long long)base + (long long)offset;
    if (target < 0) {
      target = 0;
    } else if (target > (long long)len_) {
      target = (long long)len_;
    }
    pos_ = (size_t)target;
  }

  virtual long Tell() override { return (long)pos_; }

  virtual int Error() override { return error_ ? 1 : 0; }

  virtual bool Sync() override { return true; }

  // No-op: MemoryFile is always embedded (see class comment), so the
  // FileHandle destructor must not try to free it.
  virtual void Dispose() override {}

protected:
  virtual bool Close() override { return true; }

private:
  const uint8_t *data_;
  size_t len_;
  size_t pos_;
  bool error_;
};

#endif // _MEMORY_FILE_H_
