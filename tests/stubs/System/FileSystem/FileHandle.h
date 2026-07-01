#pragma once
#include "I_File.h"
class FileHandle {
public:
  FileHandle() = default;
  explicit FileHandle(I_File *f) : f_(f) {}
  I_File *get() const { return f_; }
  I_File *operator->() const { return f_; }
  explicit operator bool() const { return f_ != nullptr; }
  void reset(I_File *f = nullptr) { if (f_) f_->Dispose(); f_ = f; }
private:
  I_File *f_ = nullptr;
};