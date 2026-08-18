/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "I_Instrument.h"
#include "../Model/Project.h"
#include "Application/Utils/char.h"
#include "System/Console/Trace.h"
#include <System/Console/nanoprintf.h>
#include <strings.h>

I_Instrument::~I_Instrument() {
  // Virtual destructor implementation
}

InstrumentType InstrumentTypeFromName(const char *name, InstrumentType unknown) {
  if (!name || name[0] == '\0') {
    return unknown;
  }
  if (!strcasecmp(name, "SYNTH")) {
    return IT_SYNTH;
  }
  for (uint i = 0; i < IT_LAST; i++) {
    if (!strcasecmp(name, InstrumentTypeNames[i])) {
      return (InstrumentType)i;
    }
  }
  return unknown;
}

// ---------------------------------------------------------------------------
// Persistence (stage 0.7, see docs/instrument-param-api.md §6).
//
// SaveContent and RestoreContent now drive exclusively from the new
// index-based parameter API. Names are looked up via FindParamByName so the
// on-disk order can change without breaking round-trip. The legacy
// Variables() pointer is used only as a back-channel to access Variable's
// richer SetString / GetString (BOOL / CHAR_LIST handling) until each
// instrument migrates to packed storage in stages 1-5.
// ---------------------------------------------------------------------------

void I_Instrument::SaveContent(tinyxml2::XMLPrinter *printer) {
  // Add firmware version information
  printer->PushAttribute("VERSION", PROJECT_NUMBER);
  // Save the instrument type
  printer->PushAttribute("TYPE", InstrumentTypeNames[GetType()]);

  // Save the instrument name. Stage 1+ will move this to idx == 0 inside the
  // unified parameter loop; until then the name is a separate PARAM child.
  if (!name_.empty()) {
    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", "InstrumentName");
    printer->PushAttribute("VALUE", name_.c_str());
    printer->CloseElement(); // PARAM
  }

  // Walk every parameter via the index-based API. FormatParamValue picks
  // the right serialisation strategy (legacy Variable::GetString while
  // Variables() is still live; GetParamFormat/GetParamValue once an
  // instrument migrates to packed storage).
  char valueBuf[64];
  int count = GetParamCount();
  const etl::ivector<Variable *> *vars = Variables();
  for (int idx = 0; idx < count; idx++) {
    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", GetParamName(idx));
    Variable *v = vars ? (*vars)[idx] : nullptr;
    if (v) {
      // Legacy path: preserve the exact existing on-disk format.
      printer->PushAttribute("VALUE", v->GetString().c_str());
    } else {
      printer->PushAttribute("VALUE", FormatParamValue(idx, valueBuf,
                                                       sizeof(valueBuf)));
    }
    printer->CloseElement(); // PARAM
  }
}

void I_Instrument::RestoreContent(PersistencyDocument *doc) {
  // First, check for TYPE attribute in the INSTRUMENT element
  bool hasAttr = doc->NextAttribute();
  while (hasAttr) {
    if (!strcasecmp(doc->attrname_, "TYPE")) {
      Trace::Log("I_INSTRUMENT", "Instrument type from XML: %s", doc->attrval_);
      // TODO: We already know the instrument type so need to validate it
      // matches the imported one here
    }
    hasAttr = doc->NextAttribute();
  }

  // Navigate to the first child of the INSTRUMENT element (which should be a
  // PARAM element)
  bool subelem = doc->FirstChild();
  int paramCount = 0;

  while (subelem) {
    // Process the PARAM element attributes
    bool hasAttr = doc->NextAttribute();
    char name[MAX_VARIABLE_STRING_LENGTH + 1] = "";
    char value[MAX_VARIABLE_STRING_LENGTH + 1] = "";
    while (hasAttr) {
      if (!strcasecmp(doc->attrname_, "NAME")) {
        strcpy(name, doc->attrval_);
      }
      if (!strcasecmp(doc->attrname_, "VALUE")) {
        strcpy(value, doc->attrval_);
      }
      hasAttr = doc->NextAttribute();
    }

    if (name[0] != '\0' && value[0] != '\0') {
      // Special handling for InstrumentName parameter
      if (!strcasecmp(name, "InstrumentName")) {
        SetName(value);
        Trace::Log("I_INSTRUMENT", "Set instrument name: %s", value);
        paramCount++;
      } else {
        int idx = FindParamByName(name);
        if (idx >= 0) {
          // Stage 0.7: legacy Variables still have richer SetString
          // handling (BOOL -> "true"/"false", CHAR_LIST -> list lookup).
          // For migrated instruments the storage path is null and we
          // parse the value via SetParamValue.
          Variable *v = Variables() ? (*Variables())[idx] : nullptr;
          if (v) {
            v->SetString(value);
            SetChanged();
            NotifyObservers();
          } else {
            SetParamValue(idx, atoi(value));
          }
          paramCount++;
        } else {
          Trace::Error("Parameter '%s' not found in instrument", name);
        }
      }
    }

    // Move to the next PARAM element
    subelem = doc->NextSibling();
  }

  // Update any UI variables that represent the instrument name
  Variable *nameVar = FindVariable(FourCC::InstrumentName);
  if (nameVar && !name_.empty()) {
    nameVar->SetString(name_.c_str());
  }
}

void I_Instrument::Purge() { ResetAllParams(); }

int I_Instrument::FindParamByName(const char *name) const {
  if (!name) {
    return -1;
  }
  int count = GetParamCount();
  for (int idx = 0; idx < count; idx++) {
    if (!strcasecmp(GetParamName(idx), name)) {
      return idx;
    }
  }
  return -1;
}

const char *I_Instrument::FormatParamValue(int idx, char *buf,
                                           size_t bufsize) const {
  if (!buf || bufsize == 0) {
    return "";
  }
  // Static storage for the formatted string. Returned by const-pointer so
  // tinyxml2::XMLPrinter can copy it; safe because SaveContent copies each
  // value before FormatParamValue is called again. Migrated instruments own
  // their ParamSpec formats; legacy facades just print the int decimal.
  const char *fmt = GetParamFormat(idx);
  npf_snprintf(buf, bufsize, fmt, GetParamValue(idx));
  return buf;
}

// ---------------------------------------------------------------------------
// New parameter API (Plan B, see docs/instrument-param-api.md).
//
// These default implementations adapt the legacy Variables() list to the
// new index-based API. Instruments that have not yet migrated to packed
// storage inherit these. Migrated instruments override them to read/write
// a packed int32 array directly. The defaults are removed in stage 7.
// ---------------------------------------------------------------------------

int I_Instrument::GetParamCount() const {
  return Variables() ? static_cast<int>(Variables()->size()) : 0;
}

FourCC I_Instrument::GetParamID(int idx) const {
  return (*Variables())[idx]->GetID();
}

const char *I_Instrument::GetParamName(int idx) const {
  return (*Variables())[idx]->GetName();
}

const char *I_Instrument::GetParamFormat(int idx) const {
  // Generic default: decimal. Migrated instruments provide their own
  // printf templates (e.g. "vol: %2.2X") via ParamSpec tables.
  (void)idx;
  return "%d";
}

int I_Instrument::GetParamMin(int idx) const {
  (void)idx;
  return 0;
}

int I_Instrument::GetParamMax(int idx) const {
  (void)idx;
  return 0xFFFF;
}

int I_Instrument::GetParamDefault(int idx) const {
  // Variable does not expose its default externally. The migrated
  // packed-storage path uses ParamSpec::default_ for an exact value.
  return -1;
}

int I_Instrument::GetParamStep(int idx) const {
  (void)idx;
  return 1;
}

int I_Instrument::GetParamBigStep(int idx) const {
  (void)idx;
  return 1;
}

int I_Instrument::GetParamValue(int idx) const {
  return (*Variables())[idx]->GetInt();
}

void I_Instrument::SetParamValue(int idx, int v) {
  Variable *v_ptr = (*Variables())[idx];
  if (v_ptr->GetInt() != v) {
    v_ptr->SetInt(v);
    // SetInt already calls SetChanged + NotifyObservers on the Variable.
    // We additionally notify on the instrument so any instrument-level
    // observers (InstrumentView) wake up.
    SetChanged();
    NotifyObservers();
  }
}

bool I_Instrument::IsParamModified(int idx) const {
  return (*Variables())[idx]->IsModified();
}

void I_Instrument::ResetParam(int idx) {
  (*Variables())[idx]->Reset();
}

void I_Instrument::ResetAllParams() {
  for (int i = 0; i < GetParamCount(); i++) {
    ResetParam(i);
  }
}
