#pragma once
#include "I_File.h"
#include "FileHandle.h"
#include <cstdint>

#define MAX_FILE_INDEX_SIZE 256
#define PFILENAME_SIZE 256
#define MAX_PROJECT_SAMPLE_PATH_LENGTH 146

namespace etl { template<typename T> class ivector; }

enum PicoFileType { PFT_UNKNOWN, PFT_FILE, PFT_DIR };

class FileSystem {
public:
  static FileSystem *GetInstance();
  FileHandle Open(const char *path, const char *mode) { return FileHandle(nullptr); }
  bool exists(const char *path) { return false; }
  bool chdir(const char *path) { return false; }
  bool DeleteFile(const char *name) { return false; }
  bool DeleteDir(const char *name) { return false; }
  bool CopyFile(const char *srcFilename, const char *destFilename) { return false; }
  bool MoveFile(const char *srcFilename, const char *destFilename) { return false; }
  bool makeDir(const char *path, bool pFlag = false) { return false; }
  void list(etl::ivector<int> *fileIndexes, const char *filter,
            bool subDirOnly, bool includeHidden = false) {}
  void getFileName(int index, char *name, int length) { name[0] = '\0'; }
  PicoFileType getFileType(int index) { return PFT_UNKNOWN; }
  uint64_t getFileSize(int index) { return 0; }
  bool isParentRoot() { return false; }
  bool isCurrentRoot() { return false; }
  bool isExFat() { return false; }
};
inline FileSystem *FileSystem::GetInstance() { static FileSystem fs; return &fs; }
