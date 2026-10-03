/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PERSISTENCE_CONSTANTS_H_
#define _PERSISTENCE_CONSTANTS_H_

#define MAX_PROJECT_NAME_LENGTH 16
// Sample filenames include the ".wav" extension.
#define MAX_INSTRUMENT_FILENAME_LENGTH 24
#define MAX_THEME_NAME_LENGTH 16
// sizeof-1 rather than strlen(): this is used as an etl::string<> template
// argument, and only GCC folds strlen() on a literal into a constant
// expression. Clang (macOS, emscripten) rejects it.
#define MAX_THEME_EXPORT_PATH_LENGTH                                           \
  (MAX_THEME_NAME_LENGTH + (sizeof(THEMES_DIR) - 1) + 1 +                      \
   (sizeof(THEME_FILE_EXTENSION) - 1))
// accounts for .pti extension so they are 4 chars shorter.
#define MAX_INSTRUMENT_NAME_LENGTH (MAX_INSTRUMENT_FILENAME_LENGTH - 4)

#define PROJECTS_DIR "/projects"
#define PROJECT_SAMPLES_DIR "samples"
#define SAMPLES_LIB_DIR "/samples"
#define INSTRUMENTS_DIR "/instruments"
#define RENDERS_DIR "/renders"
#define THEMES_DIR "/themes"
#define RECORDINGS_DIR "/recordings"
#define INSTRUMENT_FILE_EXTENSION ".pti"
#define THEME_FILE_EXTENSION ".ptt"

#define RECORDING_FILENAME "REC01.wav"

#endif // _PERSISTENCE_CONSTANTS_H_
