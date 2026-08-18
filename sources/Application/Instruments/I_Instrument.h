/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/Persistent.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/fixed.h"
#include "Application/Utils/stringutils.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/VariableContainer.h"

enum InstrumentType {
  IT_NONE = 0,
  IT_SAMPLE,
  IT_MIDI,
  IT_SID,
  IT_OPAL,
  IT_SYNTH,
  IT_LAST
};
static const char *InstrumentTypeNames[IT_LAST] = {
    "NONE", "SAMPLE", "MIDI", "SID", "OPAL", "KX1"};

// Map persisted TYPE attribute to enum (accepts legacy "SYNTH" saves).
InstrumentType InstrumentTypeFromName(const char *name,
                                      InstrumentType unknown = IT_SAMPLE);

class I_Instrument : public VariableContainer,
                     public Observable,
                     public Persistent {
protected:
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name_;

public:
  I_Instrument(etl::ivector<Variable *> *list,
               const char *nodeName = "INSTRUMENT",
               bool registerWithPersistence = false)
      : VariableContainer(list),
        Persistent(nodeName, registerWithPersistence){};
  virtual ~I_Instrument();

  // Initialisation routine

  virtual bool Init() = 0;

  // Start & stop the instument
  virtual bool Start(int channel, unsigned char note,
                     bool retrigger = true) = 0;
  virtual void Stop(int channel) = 0;

  // Engine playback  start callback

  virtual void OnStart() = 0;

  // size refers to the number of samples
  // should always fill interleaved stereo / 16bit
  // return value is true if any audio was rendered
  virtual bool Render(int channel, fixed *buffer, int size,
                      bool updateTick) = 0;

  virtual bool IsInitialized() = 0;

  virtual bool IsEmpty() = 0;

  virtual InstrumentType GetType() = 0;

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDefaultName() {
    return etl::string<MAX_INSTRUMENT_NAME_LENGTH>(
        InstrumentTypeNames[GetType()]);
  };

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetUserSetName() {
    return name_;
  };

  // Set the instrument name
  virtual void SetName(const char *name) {
    name_ = name;
    SetChanged();
    NotifyObservers();
  };

  // Set the instrument name from a Variable
  virtual void SetNameFromVariable(Variable *nameVar) {
    if (nameVar) {
      name_ = nameVar->GetString();
      SetChanged();
      NotifyObservers();
    }
  };

  // return the name to display in the UI
  // will be the user set name if available other the default name is returned
  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDisplayName() {
    auto name = GetUserSetName();
    if (!name.empty()) {
      return name;
    }
    return GetDefaultName();
  }

  virtual void ProcessCommand(int channel, FourCC cc, ushort value) = 0;

  virtual void Purge();

  virtual int GetTable() = 0;
  virtual bool GetTableAutomation() = 0;

  virtual void GetTableState(TableSaveState &state) = 0;
  virtual void SetTableState(TableSaveState &state) = 0;
  virtual const etl::ivector<Variable *> *Variables() const = 0;

  // ---------------------------------------------------------------------
  // New parameter API (Plan B, see docs/instrument-param-api.md).
  //
  // Every concrete instrument exposes its UI-facing parameters as a
  // flat indexed list. Index 0 is reserved for the instrument name
  // (FourCC::InstrumentName, displayed as "InstrumentName" in the
  // .pti file). Indices 1..N-1 are the instrument's actual synth /
  // sample / routing parameters, in instrument-defined order.
  //
  // These methods have default implementations in I_Instrument.cpp
  // that iterate the legacy Variables() list. Subclasses that have
  // not yet migrated to packed storage (stage 0.5) inherit these
  // defaults unchanged. Migrated instruments (stage 1+) override
  // them to read/write a packed int32 array.
  //
  // In stage 7 the defaults are removed and the methods become pure
  // virtual (no Variables() fallback).
  // ---------------------------------------------------------------------

  // Number of parameters (name + all UI parameters).
  virtual int GetParamCount() const;

  // Identity of parameter at idx (used for VariableID lookup, FourCC
  // matching, and persistence).
  virtual FourCC GetParamID(int idx) const;

  // Display name in .pti file and field label, e.g. "InstrumentName",
  // "OPALInstrumentAlgorithm".
  virtual const char *GetParamName(int idx) const;

  // printf format template used by UI rendering and persistence
  // serialization. e.g. "vol: %2.2X", "alg: %s".
  virtual const char *GetParamFormat(int idx) const;

  // Range and step metadata. UI uses min/max/step/bigStep for
  // clamping and acceleration.
  virtual int GetParamMin(int idx) const;
  virtual int GetParamMax(int idx) const;
  virtual int GetParamDefault(int idx) const;
  virtual int GetParamStep(int idx) const;
  virtual int GetParamBigStep(int idx) const;

  // Current value. Read by UI on every draw; written by UI on user
  // input.
  virtual int GetParamValue(int idx) const;

  // Set value. Implementations must trigger an observer notification
  // (NotifyObservers) if the value actually changed, so the UI
  // re-renders.
  virtual void SetParamValue(int idx, int v);

  // Label for a CHAR_LIST/BOOL parameter's current value, or nullptr when the
  // parameter is plain-numeric.
  //
  // UI fields render with a "%s" format for these (e.g. "wave:  %s"), so the
  // draw path MUST resolve the int to a label before formatting — passing the
  // raw int to a "%s" template dereferences it as a char* and crashes. The
  // label table is the same one persistence uses (StringParams), so the two
  // paths cannot disagree about what a value is called.
  const char *GetParamLabel(int idx) const;

  // True when idx renders as a word rather than a number.
  bool IsParamStringTyped(int idx) const { return FindStringParam(idx); }

  // State predicates.
  virtual bool IsParamModified(int idx) const;

  // Reset to default. ResetAllParams() is the bulk variant; called
  // from I_Instrument::Purge and from InstrumentView reset flows.
  virtual void ResetParam(int idx);
  virtual void ResetAllParams();

  // Persistent implementation
  virtual void SaveContent(tinyxml2::XMLPrinter *printer) override;
  virtual void RestoreContent(PersistencyDocument *doc) override;

  // Linear search for a parameter by its display/persistence name. Used by
  // RestoreContent and (via a test friend) by host tests that want to
  // round-trip NAMES without instantiating an instrument. For ≤ 36 params
  // this is faster than building a hash table. Returns -1 when the name is
  // not in this instrument's spec table.
protected:
  int FindParamByName(const char *name) const;

  // Format a parameter value at idx into a persistence-friendly string.
  // For legacy (stage 0.5) instruments this delegates to the bound
  // Variable::GetString() to preserve the on-disk format; for migrated
  // (stage 1+) instruments it uses GetParamFormat(idx) + GetParamValue(idx).
  virtual const char *FormatParamValue(int idx, char *buf,
                                       size_t bufsize) const;

  // Parse an on-disk PARAM VALUE string into the packed int representation.
  //
  // Legacy .pti files store some parameters as words rather than numbers,
  // because the pre-packed-storage code backed them with a BOOL or CHAR_LIST
  // Variable and serialised Variable::GetString(). A plain atoi() maps every
  // one of those to 0 — silently turning "true" into false and "pingpong"
  // into "none". Instruments with such parameters override this to map the
  // string back to its index; the default handles the BOOL spelling and
  // plain integers.
  virtual int ParseParamValue(int idx, const char *value) const;

  // Map `value` against a CHAR_LIST-style label table, case-insensitively.
  // Returns the matching index, or `fallback` when there is no match (which
  // includes the normal case of an already-numeric value).
  static int ParseFromList(const char *value, const char *const *labels,
                           int count, int fallback);

  // Describes one parameter whose on-disk form is a word, not a number.
  // `labels == nullptr` means a BOOL parameter ("true"/"false"); otherwise
  // the value is the index of the matching label.
  struct StringParam {
    int idx;
    const char *const *labels;
    int count;
  };

  // Instruments with BOOL/CHAR_LIST parameters return their table here; the
  // default ParseParamValue/FormatParamValue consult it, so subclasses do
  // not each need to reimplement the same switch. Returns nullptr (count 0)
  // when every parameter is plain-numeric.
  virtual const StringParam *StringParams(int &count) const {
    count = 0;
    return nullptr;
  }

  // Look up idx in StringParams(); nullptr when the param is numeric.
  const StringParam *FindStringParam(int idx) const;
};
#endif
