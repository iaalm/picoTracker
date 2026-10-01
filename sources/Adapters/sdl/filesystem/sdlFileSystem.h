/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_FILESYSTEM_H_
#define _SDL_FILESYSTEM_H_

#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include <stdio.h>

#define SDL_FS_MAX_OPEN_FILES 8

class sdlFile : public I_File {
public:
  sdlFile(FILE *file = nullptr);
  virtual ~sdlFile(){};

  virtual int Read(void *ptr, int size) override;
  virtual int GetC() override;
  virtual int Write(const void *ptr, int size, int nmemb) override;
  virtual void Seek(long offset, int whence) override;
  virtual long Tell() override;
  virtual int Error() override;
  virtual bool Sync() override;
  virtual void Dispose() override;

protected:
  virtual bool Close() override;

private:
  FILE *file_;
  // Points at this slot's entry in the owning filesystem's pool so Dispose()
  // can hand the slot back.
  bool *slotInUse_ = nullptr;
  friend class sdlFileSystem;
};

// Maps the firmware's SD-card view onto a host directory. Everything the
// tracker does is confined to the directory passed as the sandbox root, so
// running the emulator cannot touch the rest of the filesystem.
class sdlFileSystem : public FileSystem {
public:
  sdlFileSystem(const char *rootPath);
  virtual ~sdlFileSystem(){};

  virtual FileHandle Open(const char *name, const char *mode) override;
  virtual bool chdir(const char *path) override;
  virtual void list(etl::ivector<int> *fileIndexes, const char *filter,
                    bool subDirOnly, bool includeHidden = false) override;
  virtual void getFileName(int index, char *name, int length) override;
  virtual PicoFileType getFileType(int index) override;
  virtual bool isParentRoot() override;
  virtual bool isCurrentRoot() override;
  virtual bool DeleteFile(const char *name) override;
  virtual bool DeleteDir(const char *name) override;
  virtual bool exists(const char *path) override;
  virtual bool makeDir(const char *path, bool pFlag = false) override;
  virtual uint64_t getFileSize(int index) override;
  virtual bool CopyFile(const char *srcFilename,
                        const char *destFilename) override;
  virtual bool MoveFile(const char *srcFilename,
                        const char *destFilename) override;
  virtual bool isExFat() override;

private:
  // Resolves a firmware-visible path to a host path, rejecting anything that
  // would escape the sandbox root.
  bool resolve(const char *name, char *out, size_t outLen);

  // Absolute host path of the sandbox root.
  etl::string<PFILENAME_SIZE> root_;
  // Current directory, relative to root_, always starting with '/'.
  etl::string<PFILENAME_SIZE> cwd_;

  // list() hands out indexes that later calls look up by number, so the
  // listing produced by the last list() call is cached here.
  etl::vector<etl::string<PFILENAME_SIZE>, MAX_FILE_INDEX_SIZE> entries_;

  sdlFile filePool_[SDL_FS_MAX_OPEN_FILES];
  bool filePoolUsed_[SDL_FS_MAX_OPEN_FILES];
};

#endif
