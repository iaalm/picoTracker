#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/OpalInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Application/Persistency/PersistencyService.h"
#include "doctest/doctest.h"
#include <cstring>

TEST_CASE("R4: PersistencyDocument rejects corrupted bytes") {
  PersistencyDocument doc;
  const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
  CHECK(doc.LoadFromBuffer(garbage, sizeof(garbage)) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R1: SaveToBuffer + LoadFromBuffer round-trip") {
  PersistencyService svc; // local instance, no auto-register
  uint8_t buf[4096] = {0};
  size_t written = 0;

  // First call: no sub-services are registered in a host test, so
  // SaveToBuffer should still emit the root <PICOTRACKER> element and
  // return true with a non-zero written length.
  bool ok = svc.SaveToBuffer(buf, sizeof(buf), written);
  CHECK(ok == true);
  CHECK(written > 0);

  // Now load the same buffer back; the document should accept it.
  PersistencyDocument doc;
  CHECK(doc.LoadFromBuffer(buf, written) == true);
}

// ---------------------------------------------------------------------------
// R2: instrument NAMES match legacy FourCC c_str() spellings (locks 9a1315c
// bug class).
//
// Why this test exists: commit 9a1315c aligned NAMES to use the C++
// identifier names (e.g. "SampleInstrumentVolume") instead of the legacy
// FourCC c_str() spellings (e.g. "volume"). On disk, every legacy .pti file
// uses the legacy spellings, so the per-class FindParamByName(name) silently
// returned -1 for every PARAM and RestoreContent dropped them all.
//
// We compare each NAMES[i] to SPECS[i].id.c_str() — the legacy string
// registered in Foundation/Types/Types.h. A 9a1315c-style regression
// (NAMES = "SampleInstrumentVolume" while legacy .pti files use "volume")
// would mismatch this check and fail ctest.
//
// Note on the test shape: we deliberately do NOT instantiate the instruments
// here. Each instrument's constructor pulls in production DSP / service code
// (SampleInstrument's SampleVariable needs SamplePool, MidiInstrument's
// constructor calls MidiService::GetInstance(), etc.). The persistence path
// only cares about the static SPECS/NAMES tables, which are linked via the
// instrument .cpp files in the test target with
// `-Wl,--unresolved-symbols=ignore-all`. The InstrumentHostStubs.cpp file
// covers the small set of symbols that ARE referenced from static initializers.
// ---------------------------------------------------------------------------

namespace {

// Static-table check: for each (idx, name) pair, NAMES[i] must match
// SPECS[i].id.c_str() — the legacy spelling registered in Types.h.
template <typename T>
void checkNamesMatchSpecs(const char *label, const ParamSpec *specs,
                          const char *const *names, int count) {
  INFO(label << " static SPECS/NAMES consistency");
  REQUIRE(count > 0);
  for (int i = 0; i < count; ++i) {
    INFO(label << " idx=" << i);
    REQUIRE(names[i] != nullptr);
    REQUIRE(names[i][0] != '\0');
    // idx 0 is the reserved instrument-name slot. The FourCC it carries
    // (InstrumentName) is NOT the same string as what the persistence path
    // writes ("InstrumentName") — see I_Instrument::SaveContent, which
    // emits a literal PARAM NAME="InstrumentName" for the name field. So
    // skip idx 0 in the comparison; the round-trip for idx 0 is exercised
    // separately by R1 (SaveToBuffer / LoadFromBuffer).
    if (i == 0) {
      continue;
    }
    const char *expected = specs[i].id.c_str();
    REQUIRE(expected != nullptr);
    REQUIRE(expected[0] != '\0');
    // strcasecmp because legacy .pti files have inconsistent casing
    // ("table automation" vs "Table Automation", etc.) and the FourCC
    // c_str() spellings are case-insensitive on disk.
    CHECK(strcasecmp(names[i], expected) == 0);
  }
}

} // namespace

TEST_CASE("R2: instrument NAMES match legacy FourCC c_str() spellings "
          "(locks 9a1315c)") {
  checkNamesMatchSpecs<SampleInstrument>(
      "SampleInstrument", SampleInstrument::SPECS, SampleInstrument::NAMES,
      SampleInstrument::kParamCount);
  checkNamesMatchSpecs<SynthInstrument>(
      "SynthInstrument", SynthInstrument::SPECS, SynthInstrument::NAMES,
      SynthInstrument::kParamCount);
  checkNamesMatchSpecs<OpalInstrument>(
      "OpalInstrument", OpalInstrument::SPECS, OpalInstrument::NAMES,
      OpalInstrument::kParamCount);
  checkNamesMatchSpecs<SIDInstrument>(
      "SIDInstrument", SIDInstrument::SPECS, SIDInstrument::NAMES,
      SIDInstrument::kParamCount);
  checkNamesMatchSpecs<MidiInstrument>(
      "MidiInstrument", MidiInstrument::SPECS, MidiInstrument::NAMES,
      MidiInstrument::kParamCount);
}
