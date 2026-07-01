// Minimal stubs to allow PersistencyService.cpp to compile on host.
// Replaces deep include chains (I_Instrument.h -> InstrumentBank.h -> ...)
// that contain RP2040-specific static assertions.

#pragma once
#include "Application/Persistency/PersistenceConstants.h"
#include "Foundation/Types/Types.h"
#include "Externals/etl/include/etl/vector.h"
#include "Externals/etl/include/etl/string.h"
#include <cstddef>
#include <cstring>

// Variable / VariableContainer stubs
class Variable {
public:
  Variable() : dirty_(false) {}
  virtual ~Variable() {}
  virtual bool IsDirty() { return dirty_; }
  virtual void ClearDirty() { dirty_ = false; }
  virtual const char *GetString() { return ""; }
private:
  bool dirty_;
};

class VariableContainer {
public:
  VariableContainer(etl::ivector<Variable *> *list) : list_(list) {}
  virtual ~VariableContainer() {}
  Variable *FindVariable(FourCC id) { return nullptr; }
  Variable *FindVariable(const char *name) { return nullptr; }
private:
  etl::ivector<Variable *> *list_;
};

// Observable stubs
class I_Observer {};
class Observable {
public:
  Observable() : _hasChanged(false) {}
  virtual ~Observable() {}
  void AddObserver(I_Observer &o) { (void)o; }
  void RemoveObserver(I_Observer &o) { (void)o; }
  void RemoveAllObservers() {}
  void NotifyObservers(void *d = nullptr) { (void)d; }
  void SetChanged() { _hasChanged = true; }
  bool HasChanged() { return _hasChanged; }
private:
  bool _hasChanged;
};

// Instrument types
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

InstrumentType InstrumentTypeFromName(const char *name,
                                      InstrumentType unknown = IT_SAMPLE);

// Minimal I_Instrument - only what PersistencyService.cpp references
class I_Instrument : public VariableContainer, public Observable {
protected:
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name_;
public:
  I_Instrument(etl::ivector<Variable *> *list = nullptr)
      : VariableContainer(list) {}
  virtual ~I_Instrument() {}
  virtual InstrumentType GetType() = 0;
  virtual Variable *FindVariable(FourCC id) = 0;
  virtual void SetName(const char *name) {
    name_ = name;
    SetChanged();
    NotifyObservers();
  }
};

// Forward declarations for tinyxml2
namespace tinyxml2 {
class XMLPrinter;
}
class I_File;