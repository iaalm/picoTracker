#pragma once
#include <cstdio>
class I_File {
public:
  virtual ~I_File() = default;
  virtual int Read(void *ptr, int size) { return 0; }
  virtual int GetC() { return EOF; }
  virtual int Write(const void *ptr, int size, int nmemb) { return 0; }
  virtual void Seek(long offset, int whence) {}
  virtual long Tell() { return 0; }
  virtual int Error() { return 0; }
  virtual bool Sync() { return true; }
  virtual void Dispose() { delete this; }

protected:
  virtual bool Close() = 0;
};