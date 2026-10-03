/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlFileSystem.h"
#include "Application/Utils/char.h"
#include "System/Console/Trace.h"
#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
// strcasecmp lives in <strings.h> on macOS/BSD; glibc also provides it here.
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

sdlFile::sdlFile(FILE *file) : file_(file) {}

int sdlFile::Read(void *ptr, int size) {
  // The firmware's contract is "bytes read", matching fread with nmemb=1.
  return (int)fread(ptr, 1, size, file_);
}

int sdlFile::GetC() { return fgetc(file_); }

int sdlFile::Write(const void *ptr, int size, int nmemb) {
  return (int)fwrite(ptr, size, nmemb, file_);
}

void sdlFile::Seek(long offset, int whence) { fseek(file_, offset, whence); }

long sdlFile::Tell() { return ftell(file_); }

int sdlFile::Error() { return ferror(file_); }

bool sdlFile::Sync() { return fflush(file_) == 0; }

bool sdlFile::Close() {
  if (!file_) {
    return true;
  }
  bool ok = (fclose(file_) == 0);
  file_ = nullptr;
  return ok;
}

void sdlFile::Dispose() {
  Close();
  if (slotInUse_) {
    *slotInUse_ = false;
    slotInUse_ = nullptr;
  }
}

sdlFileSystem::sdlFileSystem(const char *rootPath) {
  char resolved[PATH_MAX];
  if (realpath(rootPath, resolved) == nullptr) {
    Trace::Error("FILESYSTEM: cannot resolve root path %s", rootPath);
    root_ = rootPath;
  } else {
    root_ = resolved;
  }
  cwd_ = "/";
  for (int i = 0; i < SDL_FS_MAX_OPEN_FILES; i++) {
    filePoolUsed_[i] = false;
  }
  Trace::Log("FILESYSTEM", "sandbox root: %s", root_.c_str());
}

bool sdlFileSystem::resolve(const char *name, char *out, size_t outLen) {
  // Build the firmware-relative path first, then collapse it against the
  // root so that "..", symlinks and absolute paths can't escape the sandbox.
  etl::string<PFILENAME_SIZE * 2> combined;
  if (name && name[0] == '/') {
    combined = root_.c_str();
    combined += name;
  } else {
    combined = root_.c_str();
    combined += cwd_.c_str();
    if (combined.back() != '/') {
      combined += "/";
    }
    combined += (name ? name : "");
  }

  // realpath() needs the file to exist; for new files resolve the parent
  // directory and re-attach the final component.
  char resolved[PATH_MAX];
  if (realpath(combined.c_str(), resolved) == nullptr) {
    etl::string<PFILENAME_SIZE * 2> parent = combined;
    size_t slash = parent.rfind('/');
    if (slash == etl::string<PFILENAME_SIZE * 2>::npos) {
      return false;
    }
    etl::string<PFILENAME_SIZE> leaf(parent.c_str() + slash + 1);
    parent.resize(slash);
    char resolvedParent[PATH_MAX];
    if (realpath(parent.c_str(), resolvedParent) == nullptr) {
      return false;
    }
    snprintf(resolved, PATH_MAX, "%s/%s", resolvedParent, leaf.c_str());
  }

  // Containment check: resolved must be root_ itself or sit beneath it.
  size_t rootLen = root_.size();
  if (strncmp(resolved, root_.c_str(), rootLen) != 0 ||
      (resolved[rootLen] != '\0' && resolved[rootLen] != '/')) {
    Trace::Error("FILESYSTEM: path escapes sandbox: %s", combined.c_str());
    return false;
  }

  strncpy(out, resolved, outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

FileHandle sdlFileSystem::Open(const char *name, const char *mode) {
  char path[PATH_MAX];
  if (!resolve(name, path, sizeof(path))) {
    return FileHandle();
  }

  FILE *f = fopen(path, mode);
  if (!f) {
    Trace::Error("FILESYSTEM: cannot open %s (%s)", path, strerror(errno));
    return FileHandle();
  }

  for (int i = 0; i < SDL_FS_MAX_OPEN_FILES; i++) {
    if (!filePoolUsed_[i]) {
      filePoolUsed_[i] = true;
      filePool_[i] = sdlFile(f);
      filePool_[i].slotInUse_ = &filePoolUsed_[i];
      return MakeFileHandle(&filePool_[i]);
    }
  }

  Trace::Error("FILESYSTEM: no file slots available (max %d)",
               SDL_FS_MAX_OPEN_FILES);
  fclose(f);
  return FileHandle();
}

bool sdlFileSystem::chdir(const char *path) {
  char resolved[PATH_MAX];
  if (!resolve(path, resolved, sizeof(resolved))) {
    return false;
  }
  struct stat st;
  if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }
  // Store back as a root-relative path.
  const char *rel = resolved + root_.size();
  cwd_ = (*rel == '\0') ? "/" : rel;
  Trace::Log("FILESYSTEM", "chdir -> %s", cwd_.c_str());
  return true;
}

void sdlFileSystem::list(etl::ivector<int> *fileIndexes, const char *filter,
                         bool subDirOnly, bool includeHidden) {
  fileIndexes->clear();
  entries_.clear();

  char dirPath[PATH_MAX];
  if (!resolve(".", dirPath, sizeof(dirPath))) {
    return;
  }

  DIR *dir = opendir(dirPath);
  if (!dir) {
    Trace::Error("FILESYSTEM: cannot list %s (%s)", dirPath, strerror(errno));
    return;
  }

  // Collect first so the listing can be sorted; the device returns entries in
  // directory order, but on a host that order is arbitrary and would make the
  // file browser jump around between runs.
  struct Entry {
    etl::string<PFILENAME_SIZE> name;
    bool isDir;
  };
  etl::vector<Entry, MAX_FILE_INDEX_SIZE> collected;

  struct dirent *de;
  while ((de = readdir(dir)) != nullptr &&
         collected.size() < collected.capacity()) {
    if (strcmp(de->d_name, ".") == 0) {
      continue;
    }
    bool hidden = (de->d_name[0] == '.' && strcmp(de->d_name, "..") != 0);
    if (hidden && !includeHidden) {
      continue;
    }

    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", dirPath, de->d_name);
    struct stat st;
    if (stat(full, &st) != 0) {
      continue;
    }
    bool isDir = S_ISDIR(st.st_mode);

    // ".." must always stay reachable so the browser can navigate up.
    bool isParent = (strcmp(de->d_name, "..") == 0);
    if (!isDir && subDirOnly) {
      continue;
    }
    if (!isParent && !isDir && filter && strlen(filter) > 0) {
      // Callers pass an already-lowercased filter, so lowercase the candidate
      // to make the match case-insensitive.
      char lowered[PFILENAME_SIZE];
      strncpy(lowered, de->d_name, sizeof(lowered) - 1);
      lowered[sizeof(lowered) - 1] = '\0';
      for (char *p = lowered; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
      }
      if (strstr(lowered, filter) == nullptr) {
        continue;
      }
    }

    Entry e;
    e.name = de->d_name;
    e.isDir = isDir;
    collected.push_back(e);
  }
  closedir(dir);

  // Directories first, then files, each alphabetically -- and ".." pinned to
  // the top so "go up" is always the first entry.
  std::sort(collected.begin(), collected.end(),
            [](const Entry &a, const Entry &b) {
              bool aParent = (a.name == "..");
              bool bParent = (b.name == "..");
              if (aParent != bParent) {
                return aParent;
              }
              if (a.isDir != b.isDir) {
                return a.isDir;
              }
              return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
            });

  for (size_t i = 0;
       i < collected.size() && fileIndexes->size() < fileIndexes->capacity();
       i++) {
    entries_.push_back(collected[i].name);
    fileIndexes->push_back((int)i);
  }

  Trace::Log("FILESYSTEM", "listed %d entries in %s", (int)fileIndexes->size(),
             dirPath);
}

void sdlFileSystem::getFileName(int index, char *name, int length) {
  if (index < 0 || (size_t)index >= entries_.size()) {
    if (length > 0) {
      name[0] = '\0';
    }
    return;
  }
  strncpy(name, entries_[index].c_str(), length - 1);
  name[length - 1] = '\0';
}

PicoFileType sdlFileSystem::getFileType(int index) {
  if (index < 0 || (size_t)index >= entries_.size()) {
    return PFT_UNKNOWN;
  }
  char path[PATH_MAX];
  if (!resolve(entries_[index].c_str(), path, sizeof(path))) {
    return PFT_UNKNOWN;
  }
  struct stat st;
  if (stat(path, &st) != 0) {
    return PFT_UNKNOWN;
  }
  return S_ISDIR(st.st_mode) ? PFT_DIR : PFT_FILE;
}

bool sdlFileSystem::isParentRoot() {
  // True when the parent of cwd is the sandbox root, i.e. cwd is one level
  // down: "/foo" has exactly one slash.
  if (cwd_ == "/") {
    return false;
  }
  return cwd_.find('/', 1) == etl::string<PFILENAME_SIZE>::npos;
}

bool sdlFileSystem::isCurrentRoot() { return cwd_ == "/"; }

bool sdlFileSystem::DeleteFile(const char *name) {
  char path[PATH_MAX];
  if (!resolve(name, path, sizeof(path))) {
    return false;
  }
  return unlink(path) == 0;
}

bool sdlFileSystem::DeleteDir(const char *name) {
  char path[PATH_MAX];
  if (!resolve(name, path, sizeof(path))) {
    return false;
  }
  return rmdir(path) == 0;
}

bool sdlFileSystem::exists(const char *path) {
  char resolved[PATH_MAX];
  if (!resolve(path, resolved, sizeof(resolved))) {
    return false;
  }
  struct stat st;
  return stat(resolved, &st) == 0;
}

bool sdlFileSystem::makeDir(const char *path, bool pFlag) {
  char resolved[PATH_MAX];
  if (!resolve(path, resolved, sizeof(resolved))) {
    return false;
  }

  if (!pFlag) {
    return mkdir(resolved, 0755) == 0 || errno == EEXIST;
  }

  // mkdir -p: walk the path creating each missing component. Start past the
  // root since the root itself already exists.
  char build[PATH_MAX];
  strncpy(build, resolved, sizeof(build) - 1);
  build[sizeof(build) - 1] = '\0';
  for (char *p = build + root_.size() + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(build, 0755) != 0 && errno != EEXIST) {
        return false;
      }
      *p = '/';
    }
  }
  return mkdir(build, 0755) == 0 || errno == EEXIST;
}

uint64_t sdlFileSystem::getFileSize(int index) {
  if (index < 0 || (size_t)index >= entries_.size()) {
    return 0;
  }
  char path[PATH_MAX];
  if (!resolve(entries_[index].c_str(), path, sizeof(path))) {
    return 0;
  }
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return (uint64_t)st.st_size;
}

bool sdlFileSystem::CopyFile(const char *srcFilename,
                             const char *destFilename) {
  char srcPath[PATH_MAX];
  char destPath[PATH_MAX];
  if (!resolve(srcFilename, srcPath, sizeof(srcPath)) ||
      !resolve(destFilename, destPath, sizeof(destPath))) {
    return false;
  }

  FILE *src = fopen(srcPath, "rb");
  if (!src) {
    return false;
  }
  FILE *dst = fopen(destPath, "wb");
  if (!dst) {
    fclose(src);
    return false;
  }

  char buffer[4096];
  size_t n;
  bool ok = true;
  while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
    if (fwrite(buffer, 1, n, dst) != n) {
      ok = false;
      break;
    }
  }
  if (ferror(src)) {
    ok = false;
  }
  fclose(src);
  fclose(dst);
  return ok;
}

bool sdlFileSystem::MoveFile(const char *srcFilename,
                             const char *destFilename) {
  char srcPath[PATH_MAX];
  char destPath[PATH_MAX];
  if (!resolve(srcFilename, srcPath, sizeof(srcPath)) ||
      !resolve(destFilename, destPath, sizeof(destPath))) {
    return false;
  }
  return rename(srcPath, destPath) == 0;
}

// The host filesystem is never exFAT as far as the firmware is concerned;
// this check exists to reject unsupported SD cards.
bool sdlFileSystem::isExFat() { return false; }
