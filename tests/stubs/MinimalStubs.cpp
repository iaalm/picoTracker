// Minimal stub implementations for host testing.
#include "MinimalStubs.h"

InstrumentType InstrumentTypeFromName(const char *name,
                                      InstrumentType unknown) {
  if (!name || name[0] == '\0') return unknown;
  for (int i = 1; i < IT_LAST; i++) {
    if (!strcasecmp(name, InstrumentTypeNames[i])) return (InstrumentType)i;
  }
  return unknown;
}
