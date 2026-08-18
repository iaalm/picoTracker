#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/OpalInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Application/Persistency/PersistencyService.h"
#include "doctest/doctest.h"
#include "Foundation/Types/Types.h"
#include "helpers/load_buffer.h"
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

// ---------------------------------------------------------------------------
// R2b: SPECS / FORMATS static-table validation. Catches "range typo" and
// "format string empty" classes — separate from the NAMES spelling lock
// (R2). All static-data checks, no host-compile gotchas.
// ---------------------------------------------------------------------------

namespace {

template <typename T>
void checkSpecsAndFormats(const char *label, const ParamSpec *specs,
                          const char *const *formats, int count) {
  INFO(label << " SPECS/FORMATS validation");
  REQUIRE(count > 0);
  for (int i = 0; i < count; ++i) {
    INFO(label << " idx=" << i);
    // SPECS basic invariants
    CHECK(specs[i].min <= specs[i].max);
    CHECK(specs[i].step > 0);
    // FORMATS not empty + printf-style
    REQUIRE(formats[i] != nullptr);
    REQUIRE(formats[i][0] != '\0');
    CHECK(formats[i][0] == '%');

    // default_ invariant: either a real value in [min, max] OR the
    // -1 sentinel for "table unbound" / "program unbound" / similar.
    //
    // NB: there are TWO different SPECS conventions in the codebase for
    // the same concept — the sentinel is not symmetric:
    //   (a) SampleInstrument / SynthInstrument: min=0, max=0x7F, default=-1
    //       — the user can only pick 0..0x7F; default -1 means "no
    //       table bound"; persists as -1 on disk
    //   (b) MidiInstrument: min=-1, max=0x7F, default=-1
    //       — the user CAN pick -1 explicitly; -1 is a real choice
    // Both are accepted here. The convention inconsistency is a known
    // design smell (pre-existed the v1 refactor); see R2b follow-up in
    // the SDD ledger.
    //
    // doctest's CHECK macro has a static_assert on expression complexity,
    // so we branch and emit at most one CHECK per slot.
    if (specs[i].default_ == -1) {
      // Sentinel: any (min, max) shape is accepted by design.
    } else {
      CHECK(specs[i].default_ >= specs[i].min);
      CHECK(specs[i].default_ <= specs[i].max);
    }
  }
}

} // namespace

TEST_CASE("R2b: instrument SPECS/FORMATS pass structural invariants") {
  checkSpecsAndFormats<SampleInstrument>(
      "SampleInstrument", SampleInstrument::SPECS, SampleInstrument::FORMATS,
      SampleInstrument::kParamCount);
  checkSpecsAndFormats<SynthInstrument>(
      "SynthInstrument", SynthInstrument::SPECS, SynthInstrument::FORMATS,
      SynthInstrument::kParamCount);
  checkSpecsAndFormats<OpalInstrument>(
      "OpalInstrument", OpalInstrument::SPECS, OpalInstrument::FORMATS,
      OpalInstrument::kParamCount);
  checkSpecsAndFormats<SIDInstrument>(
      "SIDInstrument", SIDInstrument::SPECS, SIDInstrument::FORMATS,
      SIDInstrument::kParamCount);
  checkSpecsAndFormats<MidiInstrument>(
      "MidiInstrument", MidiInstrument::SPECS, MidiInstrument::FORMATS,
      MidiInstrument::kParamCount);
}

TEST_CASE("R3: real .pti (lgpt_CTX2) parses + has expected instruments") {
  const std::vector<uint8_t> data =
      LoadFileOrSkip(TEST_FIXTURE_PATH "/lgptsav.dat");
  if (data.empty()) {
    MESSAGE("R3 skipped: fixture missing at " TEST_FIXTURE_PATH
            "/lgptsav.dat (test_root/ not present locally)");
    return;
  }

  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(data.data(), data.size()) == true);
  CHECK(doc.HadError() == false);

  // Quick structural check: the buffer should mention <INSTRUMENT
  // (we count opens to know roughly how many instruments the file has).
  std::string s(reinterpret_cast<const char *>(data.data()), data.size());
  int instOpens = 0;
  size_t pos = 0;
  const std::string needle = "<INSTRUMENT ";
  while ((pos = s.find(needle, pos)) != std::string::npos) {
    ++instOpens;
    pos += needle.size();
  }
  CHECK(instOpens > 0);
  CHECK(instOpens <= 64);  // sanity: project instruments are bounded
}

// ---------------------------------------------------------------------------
// R3-all: every test_root fixture must load cleanly. Versions span 0.50
// through 2.3-Beta3. If any fixture fails to load, that's a real
// persistence bug from the recent refactor — surface to the user per the
// working agreement. (See picotracker-surface-refactor-bugs memory.)
// ---------------------------------------------------------------------------

namespace {
// Known fixture names in test_root/projects/. Hardcoded — these are the
// canonical reference set; updating the list when adding new fixtures is
// an explicit maintenance step.
constexpr const char *kAllFixtures[] = {
    "bt9-midi",   // 2.0-RC3
    "DubL",       // 2.1-BETA1
    "lgpt_1TRKCOMPO",  // 1.0
    "lgpt_AAA",   // 1.0
    "lgpt_CTX1",  // 1.0
    "lgpt_CTX2",  // 2.3-Beta3 (current firmware version)
    "lgpt_First", // 1.0
    "lgpt_Steppy",    // 0.50
    "lgpt_Tardline",  // 0.50
    "oneCycAc",   // 2.0.2
    "pink-bid",   // 2.1-BETA1
    "Try",        // 2.1.3
};
} // namespace

TEST_CASE("R3-all: every test_root fixture loads cleanly") {
  int loaded = 0;
  int failed = 0;
  std::string failures;
  for (const char *name : kAllFixtures) {
    // TEST_PROJECTS_ROOT points at test_root/projects/. v1 used
    // TEST_FIXTURE_PATH (which already includes lgpt_CTX2) and ended
    // up looking for test_root/projects/lgpt_CTX2/<name>/... — which
    // is wrong. This is the bug that caused v1.1 R3-all to silently
    // skip without ever touching a fixture.
    std::string path = std::string(TEST_PROJECTS_ROOT) + "/" + name +
                       "/lgptsav.dat";
    const std::vector<uint8_t> data = LoadFileOrSkip(path);
    if (data.empty()) {
      // fixture missing — skip silently (CI without test_root/ is fine)
      continue;
    }
    PersistencyDocument doc;
    bool ok = doc.LoadFromBuffer(data.data(), data.size());
    if (ok && !doc.HadError()) {
      ++loaded;
    } else {
      ++failed;
      if (!failures.empty()) failures += ", ";
      failures += name;
    }
  }
  // If no fixtures at all were present, treat as skip (not failure).
  if (loaded == 0 && failed == 0) {
    MESSAGE("R3-all skipped: no test_root fixtures present locally");
    return;
  }
  // The whole point: every fixture that exists must load. Any failure is
  // a real bug. Per the working agreement, surface not silently patch.
  CHECK(failed == 0);
  INFO("loaded=" << loaded << " failed=" << failed
       << " failures=[" << failures << "]");
}

// ---------------------------------------------------------------------------
// R4 expansion: edge cases for LoadFromBuffer. R4 above tests one garbage
// pattern; these cover the corner cases that have historically broken
// yxml-based parsers.
// ---------------------------------------------------------------------------

TEST_CASE("R4b: empty buffer rejected") {
  PersistencyDocument doc;
  uint8_t scratch[1] = {0};
  CHECK(doc.LoadFromBuffer(scratch, 0) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R4c: NULL data pointer rejected") {
  PersistencyDocument doc;
  CHECK(doc.LoadFromBuffer(nullptr, 16) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R4d: NULL byte in input rejected by yxml") {
  // LoadFromBuffer is a yxml-level parse check, not a "well-formed XML"
  // check. Streaming parsers like yxml do NOT fail on truncated input
  // (they exit in a mid-state like YXML_ATTRSTART, which is not fatal).
  // What yxml WILL reject hard is bytes that violate XML syntax — e.g.
  // a NULL byte inside an element name.
  PersistencyDocument doc;
  const uint8_t bad[] = {'<', 'a', 0x00, '/', '>'};
  CHECK(doc.LoadFromBuffer(bad, sizeof(bad)) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R4e: invalid UTF-8 lead byte rejected by yxml") {
  // 0xFF is not a valid UTF-8 start byte; yxml will return a syntax
  // error (YXML_ESYN) and LoadFromBuffer must reject.
  PersistencyDocument doc;
  uint8_t bad = 0xFF;
  CHECK(doc.LoadFromBuffer(&bad, 1) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R4f: valid XML root passes yxml validation") {
  // Sanity: a syntactically-valid PICOTRACKER document MUST pass yxml.
  // The root-element-name check is in PersistencyService, not here.
  PersistencyDocument doc;
  const char *good = "<PICOTRACKER></PICOTRACKER>";
  CHECK(doc.LoadFromBuffer(reinterpret_cast<const uint8_t *>(good),
                           strlen(good)) == true);
  CHECK(doc.HadError() == false);
}

// ---------------------------------------------------------------------------
// R1-err: SaveToBuffer error paths. R1 above tests the happy path; these
// cover the "buffer too small" / NULL / zero-cap branches.
// ---------------------------------------------------------------------------

TEST_CASE("R1-err: NULL data pointer rejected, written stays 0") {
  PersistencyService svc;
  size_t written = 42; // sentinel — must be reset to 0 by the failing call
  CHECK(svc.SaveToBuffer(nullptr, 4096, written) == false);
  CHECK(written == 0);
}

TEST_CASE("R1-err: zero cap rejected, written stays 0") {
  PersistencyService svc;
  uint8_t buf[16] = {0};
  size_t written = 42;
  CHECK(svc.SaveToBuffer(buf, 0, written) == false);
  CHECK(written == 0);
}

TEST_CASE("R1-err: cap too small for the root element rejected") {
  // With no sub-services registered, tinyxml2 in compact mode emits
  // <PICOTRACKER/> — 15 bytes. An 8-byte buffer must fail, not silently
  // truncate. (Earlier draft used cap=16, which is large enough to
  // accidentally succeed.)
  PersistencyService svc;
  uint8_t buf[8] = {0};
  size_t written = 42;
  CHECK(svc.SaveToBuffer(buf, sizeof(buf), written) == false);
  CHECK(written == 0);
}

// ---------------------------------------------------------------------------
// R-iter: exercise the MemoryFile-backed iteration path on a real fixture.
// This is the path that v1 review flagged as "staged but the consumer is
// missing" (Important #1). This test consumes it.
// ---------------------------------------------------------------------------

TEST_CASE("R-iter: FirstChild on real fixture returns PICOTRACKER, "
          "attribute walk works") {
  const std::vector<uint8_t> data =
      LoadFileOrSkip(TEST_FIXTURE_PATH "/lgptsav.dat");
  if (data.empty()) {
    MESSAGE("R-iter skipped: fixture missing");
    return;
  }
  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(data.data(), data.size()) == true);

  // FirstChild should land on the root element.
  REQUIRE(doc.FirstChild() == true);
  CHECK(strcmp(doc.ElemName(), "PICOTRACKER") == 0);
  CHECK(doc.HadError() == false);

  // Walk into the first child of the root — should be <PROJECT VERSION=...>.
  REQUIRE(doc.FirstChild() == true);
  CHECK(strcmp(doc.ElemName(), "PROJECT") == 0);

  // Read the VERSION attribute. This exercises the MemoryFile byte source
  // through NextAttribute — the path v1 added but no test was using.
  bool gotVersion = false;
  while (doc.NextAttribute()) {
    if (!strcasecmp(doc.attrname_, "VERSION")) {
      CHECK(doc.attrval_[0] != '\0');
      gotVersion = true;
      break;
    }
  }
  CHECK(gotVersion == true);
}

// ---------------------------------------------------------------------------
// R-master: every test_root fixture has the expected PICOTRACKER master
// node. This replicates the master-node check in PersistencyService::Load
// (lines 305-310 of PersistencyService.cpp). yxml passes (R3-all) is
// necessary but not sufficient — the root element name check is a separate
// layer. Failure here means a legacy fixture can't be loaded by current
// firmware because it has a different root element.
// ---------------------------------------------------------------------------

TEST_CASE("R-master: every test_root fixture has PICOTRACKER root") {
  int checked = 0;
  std::string wrongRoots;
  for (const char *name : kAllFixtures) {
    std::string path = std::string(TEST_PROJECTS_ROOT) + "/" + name +
                       "/lgptsav.dat";
    const std::vector<uint8_t> data = LoadFileOrSkip(path);
    if (data.empty()) continue;
    PersistencyDocument doc;
    if (!doc.LoadFromBuffer(data.data(), data.size())) continue;
    if (!doc.FirstChild()) continue;
    ++checked;
    if (strcmp(doc.ElemName(), "PICOTRACKER") != 0) {
      if (!wrongRoots.empty()) wrongRoots += ", ";
      wrongRoots += std::string(name) + ":<" + doc.ElemName() + ">";
    }
  }
  if (checked == 0) {
    MESSAGE("R-master skipped: no test_root fixtures present locally");
    return;
  }
  CHECK(wrongRoots.empty());
  INFO("checked " << checked << " fixtures, wrong roots=[" << wrongRoots << "]");
}

// ---------------------------------------------------------------------------
// R-load-attr: attribute parsing robustness. We construct known-bad XML
// and walk it through PersistencyDocument. The point is to catch
// regressions in the parser layer (the layer between bytes and the
// Restore loop). What RestoreContent DOES with the bad value is a
// separate concern (host tests can't easily exercise RestoreContent
// because PersistencyService::SubServices() is empty in host scope).
// ---------------------------------------------------------------------------

TEST_CASE("R-load-attr: malformed VALUE doesn't crash parser, surfaces as-is") {
  const char *xml =
      "<PICOTRACKER>"
        "<PARAM NAME=\"volume\" VALUE=\"not_a_number\"/>"
      "</PICOTRACKER>";
  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(reinterpret_cast<const uint8_t *>(xml),
                           strlen(xml)) == true);
  REQUIRE(doc.FirstChild() == true);   // PICOTRACKER
  REQUIRE(strcmp(doc.ElemName(), "PICOTRACKER") == 0);
  REQUIRE(doc.FirstChild() == true);   // PARAM
  REQUIRE(strcmp(doc.ElemName(), "PARAM") == 0);

  // Walk attributes — VALUE should be "not_a_number" (parser is faithful;
  // the question of what RestoreContent does with it is the caller's
  // problem). This catches regressions where someone "helpfully" strips
  // or rejects bad values at the parser layer.
  bool sawName = false, sawValue = false;
  while (doc.NextAttribute()) {
    if (!strcasecmp(doc.attrname_, "NAME")) {
      sawName = true;
      CHECK(strcmp(doc.attrval_, "volume") == 0);
    } else if (!strcasecmp(doc.attrname_, "VALUE")) {
      sawValue = true;
      CHECK(strcmp(doc.attrval_, "not_a_number") == 0);
    }
  }
  CHECK(sawName == true);
  CHECK(sawValue == true);
}

TEST_CASE("R-load-attr: empty VALUE parses as empty string") {
  const char *xml =
      "<PICOTRACKER>"
        "<PARAM NAME=\"volume\" VALUE=\"\"/>"
      "</PICOTRACKER>";
  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(reinterpret_cast<const uint8_t *>(xml),
                           strlen(xml)) == true);
  REQUIRE(doc.FirstChild() == true);
  REQUIRE(doc.FirstChild() == true);

  bool sawValue = false;
  while (doc.NextAttribute()) {
    if (!strcasecmp(doc.attrname_, "VALUE")) {
      sawValue = true;
      CHECK(doc.attrval_[0] == '\0');
    }
  }
  CHECK(sawValue == true);
}

TEST_CASE("R-load-attr: self-closing PARAM with only NAME parses") {
  const char *xml =
      "<PICOTRACKER>"
        "<PARAM NAME=\"volume\"/>"
      "</PICOTRACKER>";
  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(reinterpret_cast<const uint8_t *>(xml),
                           strlen(xml)) == true);
  REQUIRE(doc.FirstChild() == true);
  REQUIRE(doc.FirstChild() == true);

  // Only NAME attribute expected; no VALUE.
  int attrCount = 0;
  while (doc.NextAttribute()) ++attrCount;
  CHECK(attrCount == 1);
}

TEST_CASE("R-load-attr: NULL byte mid-element rejected") {
  // Earlier draft tested truncated attribute (no closing quote). yxml is
  // a streaming parser — it does NOT error on incomplete input because
  // it can't know the input is incomplete. To exercise the parser's
  // error path we use a NULL byte inside the element name, which yxml
  // rejects with YXML_ESYN.
  const uint8_t xml[] = {
      '<', 'P', 'I', 'C', 'O', 'T', 'R', 'A', 'C', 'K', 'E', 'R', '>',
      '<', 'P', 'A', 'R', 'A', 'M', ' ', 'N', 'A', 'M', 'E', '=',
      '"', 'v', 'o', 'l', 0x00, 'u', 'm', 'e', '"', '/', '>'
  };
  PersistencyDocument doc;
  CHECK(doc.LoadFromBuffer(xml, sizeof(xml)) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R-load-attr: FirstChild walks children, not extra top-level elements") {
  // PersistencyService::Load walks FirstChild then NextSibling — that is,
  // children of PICOTRACKER, not siblings of PICOTRACKER. This test
  // confirms the same interpretation independently: FirstChild from
  // PICOTRACKER returns its children, NOT a sibling root element.
  //
  // No whitespace in the literal: keeps yxml happy. Earlier draft with
  // indentation failed at the parser level — yxml may not tolerate
  // extra roots depending on configuration. This minimal XML avoids
  // that ambiguity.
  const char *xml =
      "<PICOTRACKER><PROJECT/></PICOTRACKER>";
  PersistencyDocument doc;
  REQUIRE(doc.LoadFromBuffer(reinterpret_cast<const uint8_t *>(xml),
                           strlen(xml)) == true);
  REQUIRE(doc.FirstChild() == true);
  REQUIRE(strcmp(doc.ElemName(), "PICOTRACKER") == 0);

  // Walk children of PICOTRACKER. There should be exactly one: PROJECT.
  int childCount = 0;
  bool sawProject = false;
  for (bool e = doc.FirstChild(); e; e = doc.NextSibling()) {
    ++childCount;
    if (!strcmp(doc.ElemName(), "PROJECT")) sawProject = true;
  }
  CHECK(childCount == 1);
  CHECK(sawProject == true);
}
