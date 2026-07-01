/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PARAM_SPEC_H_
#define _PARAM_SPEC_H_

#include <cstdint>
#include "Foundation/Types/Types.h" // for FourCC

// ---------------------------------------------------------------------------
// ParamSpec — static metadata for one instrument parameter (Plan B,
// docs/instrument-param-api.md §4).
//
// Lives in flash on RP2040 (`static const`). The table is the only source of
// per-parameter metadata — no runtime allocation, no virtual calls. Each
// instrument class provides one ParamSpec per UI parameter (including the
// reserved name slot at idx 0).
//
// Field types are widened from the design-doc's `uint8_t` because SID/Synth
// parameters reach 0xFFFF — `uint8_t` cannot represent -1 (Table default) or
// 16-bit max values. The on-flash cost rises from 12 to 16 bytes per spec,
// still negligible (≈ 560 B for 35 Synth params).
//
// The NAMES and FORMATS tables are indexed via name_off / format_off (offsets
// into per-class arrays of `const char *`). Lookup is
// `NAMES[SPECS[i].name_off]`.
// ---------------------------------------------------------------------------
struct ParamSpec {
  FourCC  id;          // 1 B (1 byte pad follows for alignment)
  uint8_t _pad0;       // 1 B
  uint16_t name_off;   // 2 B offset into per-class name string table
  uint16_t format_off; // 2 B offset into per-class format string table
  int32_t  default_;   // 4 B (signed: -1 Table unbound, 0xF1C8 OPAL ADSR,
                       //            0xFFFF Synth LFODelay all fit comfortably)
  int16_t  min;        // 2 B (signed: -1 for Table/Program "off" sentinel)
  uint16_t max;        // 2 B
  uint8_t  step;       // 1 B
  uint8_t  big_step;   // 1 B
  uint8_t  _pad1;      // 1 B (alignment / future flags)
  uint8_t _pad2;       // 1 B (round struct to 4-byte alignment for default_)
};
#ifndef HOST_TEST
// On host, ETL's enum_type wraps FourCC as a 4-byte object instead of char
// (RP2040 char-sized); this padding cascades through ParamSpec and breaks the
// on-device 20-byte layout. The on-device budget is enforced by the firmware
// build; the host build only needs ParamSpec to compile.
static_assert(sizeof(ParamSpec) == 20, "ParamSpec must be 20 B");
#endif

#endif
