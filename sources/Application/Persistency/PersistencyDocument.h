/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PERSISTENCY_DOCUMENT_H_
#define _PERSISTENCY_DOCUMENT_H_

#include "Externals/yxml/yxml.h"
#include "MemoryFile.h"
#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"

class PersistencyDocument {
public:
  PersistencyDocument();
  ~PersistencyDocument(); // Add destructor
  bool Load(const char *filename);
  bool LoadFromBuffer(const uint8_t *data, size_t len);
  void Close(); // Add method to explicitly close the file

  // r_ < YXML_OK to signal that the xml parsing had a fatal error
  bool HadError() const { return r_ < YXML_OK; }

  bool FirstChild();
  bool NextSibling();
  bool NextAttribute();
  bool HasContent();
  char *ElemName();

  char attrname_[64];
  char attrval_[64];
  char content_[129]; // 128 + \0
  yxml_ret_t r_;

  int version_;

private:
  inline static char stack_[1024];
  inline static yxml_t state_[1];
  FileHandle fp_;
  // Embedded MemoryFile so LoadFromBuffer can hand the iteration
  // methods an I_File* without heap-allocating. Dispose() on the
  // MemoryFile is a no-op; FileHandle just holds a non-owning pointer.
  MemoryFile memory_fp_;
};
#endif
