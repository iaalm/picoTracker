# picoTracker Host Testing v1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the picoTracker persistence path host-testable (no RP2040, no HAL, no runtime overhead) so the next NAMES/string-mismatch round-trip bug fails `ctest` instead of firmware flash.

**Architecture:** Add buffer-based load/save seams to `PersistencyDocument` (yxml reader) and `PersistencyService` (save loop). Test code constructs `std::vector<uint8_t>` and calls into the same code production calls into — no abstraction, no vtable, no HAL. Refactor `PersistencyService::Load`/`Save` to read/write the file then call the new buffer methods, so production goes through the same seam.

**Tech Stack:** C++23, doctest, tinyxml2 (write path), yxml (read path), etl, CMake.

## Spec revision (vs. design doc)

The design spec placed the seam on `Project::LoadFrom/SaveTo`. On reading the
code (`sources/Application/Persistency/Persistent.cpp:17-29`,
`PersistencyService.cpp:181-326`), the actual layering is:

- `Project` is a `Persistent` (subclass of `SubService`).
- `Persistent::Save(tinyxml2::XMLPrinter*)` and `Persistent::Restore(PersistencyDocument*)`
  are the existing save/load seams driven by virtual `SaveContent`/`RestoreContent`.
- `PersistencyService` iterates the global `SubServices()` list and calls Save/Restore on each.

The new APIs therefore live on the layer that already holds the loop:
- `PersistencyDocument::LoadFromBuffer(const uint8_t*, size_t)` — buffer-based yxml reader
- `PersistencyService::SaveToBuffer(uint8_t*, size_t, size_t&)` — buffer-based tinyxml2 writer
- `PersistencyService::LoadFromBuffer(const uint8_t*, size_t)` — uses doc + subservice loop
- `PersistencyService::Load(name)` / `Save(...)` — read/write file then call the buffer methods

`Project` and the instrument classes are **not modified** — they continue to
implement `SaveContent`/`RestoreContent` as today. The buffer APIs drive
those callbacks the same way the file APIs do.

## Global constraints

- **Zero runtime cost on RP2040.** New methods are non-virtual; the yxml
  loop in `LoadFromBuffer` is a small duplication of the yxml loop in
  `Load(filename)`, both compiled into production. No vtable, no function
  pointer, no heap allocation in the hot path.
- **No new dependencies.** `tinyxml2`, `yxml`, `etl`, `doctest` already
  linked. `tests/CMakeLists.txt` already provides the host test scaffold
  (lines 1–47).
- **Surface the unexpected.** If a test surfaces a latent bug from the
  recent `stage 0–5` refactor (mismatched name, silent default, off-by-one
  in serialization), **stop and tell the user** — do not silently patch
  around it. Per the working agreement saved in memory.
- **R3 fixture path** is `/home/simon/Projects/picoTracker/test_root/projects/lgpt_CTX2/lgptsav.dat`
  (only project at firmware version `2.3-Beta3`). `test_root/` is git-ignored.
  Tests must **skip R3 with a clear message** if the file is missing —
  CI without the data must still run R1/R2/R4.

## File structure

### Created
- `tests/persist_tests.cpp` — 4 doctest cases (R1/R2/R3/R4)
- `tests/helpers/load_buffer.h` — read a file into `std::vector<uint8_t>` (header-only)
- `tests/stubs/System/FileSystem/FileSystem.h` — minimal host stub (returns nullptr, holds no state)
- `tests/stubs/System/FileSystem/I_File.h` — minimal host stub
- `tests/stubs/System/FileSystem/FileHandle.h` — minimal host stub

### Modified
- `sources/Application/Persistency/PersistencyDocument.h` — declare `LoadFromBuffer`
- `sources/Application/Persistency/PersistencyDocument.cpp` — implement `LoadFromBuffer` (yxml loop from buffer)
- `sources/Application/Persistency/PersistencyService.h` — declare `SaveToBuffer` + `LoadFromBuffer`
- `sources/Application/Persistency/PersistencyService.cpp` — implement + refactor `Load`/`SaveProjectData`
- `tests/CMakeLists.txt` — add `picoTracker_persist` target, link new sources

---

## Task 1: cmake scaffold for `picoTracker_persist`

**Files:**
- Modify: `tests/CMakeLists.txt`
- Create: `tests/stubs/System/FileSystem/I_File.h`, `FileSystem.h`, `FileHandle.h`
- Create: `tests/helpers/load_buffer.h`

**Step 1.1: Add host-only FileSystem stubs**

`tests/stubs/System/FileSystem/I_File.h`:
```cpp
#pragma once
#include <cstdio>
class I_File {
public:
  virtual ~I_File() = default;
  virtual int GetC() = 0;
  virtual bool Close() = 0;
  virtual void Dispose() { delete this; }
};
```

`tests/stubs/System/FileSystem/FileSystem.h`:
```cpp
#pragma once
#include "I_File.h"
#include "FileHandle.h"
class FileSystem {
public:
  static FileSystem *GetInstance();
  FileHandle Open(const char *path, const char *mode) { return FileHandle(nullptr); }
  bool exists(const char *path) { return false; }
};
inline FileSystem *FileSystem::GetInstance() { static FileSystem fs; return &fs; }
```

`tests/stubs/System/FileSystem/FileHandle.h`:
```cpp
#pragma once
#include "I_File.h"
class FileHandle {
public:
  FileHandle() = default;
  explicit FileHandle(I_File *f) : f_(f) {}
  I_File *get() const { return f_; }
  I_File *operator->() const { return f_; }
  explicit operator bool() const { return f_ != nullptr; }
  void reset(I_File *f = nullptr) { if (f_) f_->Dispose(); f_ = f; }
private:
  I_File *f_ = nullptr;
};
```

**Step 1.2: Add `load_buffer.h` helper**

`tests/helpers/load_buffer.h`:
```cpp
#pragma once
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>

inline std::vector<uint8_t> LoadFileOrSkip(const std::string &path) {
  std::vector<uint8_t> out;
  std::ifstream f(path, std::ios::binary);
  if (!f) return out;  // empty = caller should skip
  f.seekg(0, std::ios::end);
  auto sz = f.tellg();
  f.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(sz));
  f.read(reinterpret_cast<char *>(out.data()), sz);
  return out;
}
```

**Step 1.3: Add `picoTracker_persist` target**

In `tests/CMakeLists.txt`, after the existing `add_executable(synth_render ...)` block, add:

```cmake
add_executable(picoTracker_persist
  test_main.cpp
  persist_tests.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Persistency/PersistencyService.cpp
  ../sources/Application/Persistency/Persistent.cpp
)

target_include_directories(picoTracker_persist PRIVATE ${HOST_TEST_INCLUDES})

target_compile_definitions(picoTracker_persist PRIVATE
  HOST_TEST
  TEST_FIXTURE_PATH="${CMAKE_CURRENT_LIST_DIR}/test_root/projects/lgpt_CTX2/lgptsav.dat"
)
```

The target initially has **no tests** — `persist_tests.cpp` is empty (just the `doctest` smoke). The build verifies the target compiles and links before we add real tests.

**Step 1.4: Add `persist_tests.cpp` stub**

`tests/persist_tests.cpp`:
```cpp
#include "doctest/doctest.h"
TEST_CASE("persist smoke") { CHECK(true); }
```

**Step 1.5: Build**

From project root:
```bash
cmake -S tests -B build-host
cmake --build build-host --target picoTracker_persist -j 4
```
Expected: build succeeds (warnings OK). Run `./build-host/picoTracker_persist` — expect `1 assertion in 1 test case`.

**Step 1.6: Commit**

```bash
git add tests/CMakeLists.txt tests/persist_tests.cpp \
  tests/stubs/System/FileSystem/ tests/helpers/load_buffer.h
git commit -m "tests: add picoTracker_persist cmake scaffold + FileSystem stubs"
```

---

## Task 2: Test R4 — corrupted input guard (red)

The buffer API does not exist yet. This test is the failing red.

**Files:**
- Modify: `tests/persist_tests.cpp`

**Step 2.1: Write the test**

Replace `tests/persist_tests.cpp` with:
```cpp
#include "Application/Persistency/PersistencyDocument.h"
#include "doctest/doctest.h"

TEST_CASE("R4: PersistencyDocument rejects corrupted bytes") {
  PersistencyDocument doc;
  const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
  CHECK(doc.LoadFromBuffer(garbage, sizeof(garbage)) == false);
  CHECK(doc.HadError() == true);
}
```

**Step 2.2: Run — expect compile failure**

```bash
cmake --build build-host --target picoTracker_persist -j 4 2>&1 | tail -10
```
Expected: error `LoadFromBuffer` not a member of `PersistencyDocument`. This is the red.

**Step 2.3: Commit (test-only)**

```bash
git add tests/persist_tests.cpp
git commit -m "tests: add R4 (corrupted input) failing test"
```

---

## Task 3: Declare + implement `LoadFromBuffer` (green for R4)

**Files:**
- Modify: `sources/Application/Persistency/PersistencyDocument.h`
- Modify: `sources/Application/Persistency/PersistencyDocument.cpp`

**Step 3.1: Declare in header**

Add to `PersistencyDocument.h` inside `class PersistencyDocument`, after `bool Load(const char *filename);`:
```cpp
  bool LoadFromBuffer(const uint8_t *data, size_t len);
```

**Step 3.2: Implement in `.cpp`**

Add at the end of `PersistencyDocument.cpp`:
```cpp
bool PersistencyDocument::LoadFromBuffer(const uint8_t *data, size_t len) {
  if (!data || len == 0) {
    Trace::Error("PERSISTENCYDOCUMENT: empty buffer");
    r_ = YXML_ESYN;
    return false;
  }

  yxml_init(state_, stack_, sizeof(stack_));
  r_ = YXML_OK;

  for (size_t i = 0; i < len; ++i) {
    r_ = yxml_parse(state_, data[i]);
    if (r_ < YXML_OK) {
      Trace::Error("PERSISTENCYDOCUMENT: buffer parse error %d at byte %zu",
                   r_, i);
      return false;
    }
  }
  return true;
}
```

The subsequent `FirstChild`/`NextSibling`/`NextAttribute`/`HasContent` methods keep using `fp_->GetC()` — for v1 the buffer load only **validates** the XML; iterating children still requires a backing reader. R4 only asserts parse-level rejection, which the loop above provides. (v2 task: switch the iteration methods to use the in-memory cursor.)

**Step 3.3: Build + run R4**

```bash
cmake --build build-host --target picoTracker_persist -j 4
./build-host/picoTracker_persist
```
Expected: R4 passes (corrupt bytes are rejected). The smoke test also passes. Total: 2 tests, 3 assertions.

**Step 3.4: Commit**

```bash
git add sources/Application/Persistency/PersistencyDocument.h \
  sources/Application/Persistency/PersistencyDocument.cpp
git commit -m "PersistencyDocument: add LoadFromBuffer (validates XML from bytes)"
```

---

## Task 4: Test R1 — round-trip empty document (red)

**Files:**
- Modify: `tests/persist_tests.cpp`

**Step 4.1: Write the test**

Append to `tests/persist_tests.cpp`:
```cpp
#include "Application/Persistency/PersistencyService.h"

TEST_CASE("R1: SaveToBuffer + LoadFromBuffer round-trip") {
  PersistencyService svc;  // local instance, no auto-register
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
```

> **Spec note (plan fix during pre-flight):** `Status` is a class with static
> `Set()` methods, not an enum (see `sources/System/io/Status.h`). The
> codebase's success/failure conventions are `bool` (e.g. `PersistencyDocument::Load`)
> and `PersistencyResult` (e.g. `PersistencyService::Load`). `SaveToBuffer`
> returns `bool` to match `LoadFromBuffer`'s `bool` return — symmetric and
> requires no new enum.

**Step 4.2: Run — expect compile failure**

```bash
cmake --build build-host --target picoTracker_persist -j 4 2>&1 | tail -5
```
Expected: `SaveToBuffer` not found on `PersistencyService`. This is the red.

**Step 4.3: Commit (test-only)**

```bash
git add tests/persist_tests.cpp
git commit -m "tests: add R1 (round-trip) failing test"
```

---

## Task 5: Implement `SaveToBuffer` (green for R1)

**Files:**
- Modify: `sources/Application/Persistency/PersistencyService.h`
- Modify: `sources/Application/Persistency/PersistencyService.cpp`

**Step 5.1: Declare in header**

Add to `PersistencyService.h`, in `public:`:
```cpp
  bool SaveToBuffer(uint8_t *data, size_t cap, size_t &written);
```

**Step 5.2: Implement in `.cpp`**

Add at the end of `PersistencyService.cpp`:
```cpp
#include <Externals/TinyXML2/tinyxml2.h>

bool PersistencyService::SaveToBuffer(uint8_t *data, size_t cap,
                                     size_t &written) {
  written = 0;
  if (!data || cap == 0) {
    Trace::Error("PERSISTENCYSERVICE: SaveToBuffer called with empty target");
    return false;
  }

  tinyxml2::XMLPrinter printer;  // internal buffer (no FILE*)
  printer.OpenElement("PICOTRACKER");

  // Drive the same Persistent::Save loop that SaveProjectData uses,
  // but with the in-memory printer.
  for (auto *sub : SubServices()) {
    auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
    currentItem->Save(&printer);
  }
  printer.CloseElement();

  const char *xml = printer.CStr();
  if (!xml) {
    Trace::Error("PERSISTENCYSERVICE: XMLPrinter produced no output");
    return false;
  }
  size_t len = strlen(xml);
  if (len + 1 > cap) {
    Trace::Error("PERSISTENCYSERVICE: SaveToBuffer cap %zu < needed %zu",
                 cap, len + 1);
    return false;
  }
  memcpy(data, xml, len + 1);  // include trailing NUL for symmetry
  written = len;
  return true;
}
```

Note on the `tinyxml2::XMLPrinter` constructor: confirm the no-arg / pointer-null form by reading `sources/Externals/TinyXML2/tinyxml2.h:2105-2115`. If the constructor signature differs (e.g. requires `FILE*` or only has the no-arg form), adjust accordingly — the goal is "write to internal buffer; retrieve via `CStr()`". If the no-arg form is unavailable, use the form `tinyxml2::XMLPrinter printer;` (default ctor) per the tinyxml2 examples in the same header.

**Step 5.3: Build + run R1 + R4**

```bash
cmake --build build-host --target picoTracker_persist -j 4
./build-host/picoTracker_persist
```
Expected: R1 + R4 pass. Total: 3 tests, 6 assertions.

**Step 5.4: Commit**

```bash
git add sources/Application/Persistency/PersistencyService.h \
  sources/Application/Persistency/PersistencyService.cpp
git commit -m "PersistencyService: add SaveToBuffer (writes XML to memory)"
```

---

## Task 6: Test R2 — NAMES table integrity (locks 9a1315c bug class)

This test catches the **exact** class of bug from commit `9a1315c`
(NAMES table spelled with C++ identifiers, silently dropping every legacy
`.pti` PARAM on load). It does **not** require saving/loading — it
exercises the read/write names directly, which is where the bug lived.

**Files:**
- Modify: `tests/persist_tests.cpp`

**Step 6.1: Write the test**

Append to `tests/persist_tests.cpp`:
```cpp
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Instruments/OpalInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Foundation/Types/Types.h"

namespace {

// Round-trip every named param of one instrument through the public API.
// For each (idx, name) pair the test asserts:
//   1. GetParamName(idx) returns a name (non-null, non-empty)
//   2. FindParamByName(name) returns the same idx
// A mismatch here is exactly the 9a1315c class of bug.
void checkRoundTripNames(const char *label, I_Instrument &inst) {
  int n = inst.GetParamCount();
  INFO(label << " has " << n << " params");
  REQUIRE(n > 0);
  for (int i = 0; i < n; ++i) {
    const char *name = inst.GetParamName(i);
    INFO(label << " idx=" << i);
    REQUIRE(name != nullptr);
    REQUIRE(name[0] != '\0');
    int found = inst.FindParamByName(name);
    CHECK(found == i);  // the 9a1315c-style bug: name lookup fails
  }
}

}  // namespace

TEST_CASE("R2: instrument NAMES round-trip (locks 9a1315c)") {
  SampleInstrument si;
  checkRoundTripNames("SampleInstrument", si);
  SynthInstrument syi;
  checkRoundTripNames("SynthInstrument", syi);
  OpalInstrument opi;
  checkRoundTripNames("OpalInstrument", opi);
  SIDInstrument sidi;
  checkRoundTripNames("SIDInstrument", sidi);
  MidiInstrument mi;
  checkRoundTripNames("MidiInstrument", mi);
}
```

This requires `I_Instrument::GetParamName` and `FindParamByName` to be
exercisable on a default-constructed instrument. The `stage 0` commit
added these to the `I_Instrument` interface; check
`sources/Application/Instruments/I_Instrument.h:151,192` and
`I_Instrument.cpp:157-201` for current shape and adjust the assertion if
the API differs (e.g. `GetParamName` may return a `const char *` from a
per-class `NAMES` table; `FindParamByName` returns `int` index).

If an instrument class does not host-compile (depends on DSP code,
`braids::`, OPL/SID emulator), stub it minimally at link time — that
work happens here if the build fails, not in advance. Possible stubs:
- A `NoneInstrument` default instance that returns 0 params
- For `SynthInstrument`: `braids::` may not host-compile — fall back to
  asserting that `GetParamName`/`FindParamByName` exist for a small
  hand-built test instrument class added in `tests/`.

**Step 6.2: Build + run**

```bash
cmake --build build-host --target picoTracker_persist -j 4
./build-host/picoTracker_persist
```
Expected: 4 tests, many assertions (R2 covers ~5 inst × ~20 params). If a 9a1315c-class bug exists in any of the 5 instruments, `CHECK(found == i)` will fail for the affected idx and the user is to be told immediately (per the surface-refactor-bugs memory).

**Step 6.3: If R2 surfaces a bug, STOP and tell the user.** Do not patch around it. The user's working agreement: surface latent refactor bugs, decide together.

**Step 6.4: Commit**

```bash
git add tests/persist_tests.cpp
git commit -m "tests: add R2 (instrument NAMES round-trip, locks 9a1315c)"
```

---

## Task 7: Test R3 — real `.pti` file parses + basic invariants

**Files:**
- Modify: `tests/persist_tests.cpp`

**Step 7.1: Write the test**

Append to `tests/persist_tests.cpp`:
```cpp
#include "helpers/load_buffer.h"
#include <cstdio>

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
```

`TEST_FIXTURE_PATH` is a `HOST_TEST` compile definition set in
`tests/CMakeLists.txt` (Task 1.3). The macro is the absolute path
to `test_root/projects/lgpt_CTX2/`, so the full path becomes
`<…>/test_root/projects/lgpt_CTX2/lgptsav.dat`.

If the cmake `add_definitions` form strips quotes around the path, the
test uses string literal concatenation with a `//` separator or a `+`
join — adjust per the actual macro expansion.

**Step 7.2: Build + run**

```bash
cmake --build build-host --target picoTracker_persist -j 4
./build-host/picoTracker_persist
```
Expected: R3 passes (or skips with the message). R1/R2/R4 still pass.

**Step 7.3: Commit**

```bash
git add tests/persist_tests.cpp
git commit -m "tests: add R3 (real project file smoke, skip-if-missing)"
```

---

## Task 8: Refactor `PersistencyService::Load` to use `LoadFromBuffer`

**Files:**
- Modify: `sources/Application/Persistency/PersistencyService.cpp`
- Modify: `sources/Application/Persistency/PersistencyService.h`

**Step 8.1: Declare `LoadFromBuffer` on the service**

In `PersistencyService.h`, add (in `public:`):
```cpp
  PersistencyResult LoadFromBuffer(const uint8_t *data, size_t len);
```

**Step 8.2: Implement in `.cpp`**

Add at the end of `PersistencyService.cpp`:
```cpp
PersistencyResult
PersistencyService::LoadFromBuffer(const uint8_t *data, size_t len) {
  PersistencyDocument doc;
  if (!doc.LoadFromBuffer(data, len)) {
    return PERSIST_LOAD_FAILED;
  }

  bool elem = doc.FirstChild();
  if (!elem || strcmp(doc.ElemName(), "PICOTRACKER")) {
    Trace::Error("could not find master node");
    return PERSIST_LOAD_FAILED;
  }

  elem = doc.FirstChild();
  while (elem) {
    for (auto *sub : SubServices()) {
      auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
      if (currentItem->Restore(&doc)) {
        break;
      }
    }
    elem = doc.NextSibling();
  }
  if (doc.HadError()) {
    Trace::Error("XML errors detected while loading project from buffer");
    return PERSIST_LOAD_FAILED;
  }
  return PERSIST_LOADED;
}
```

**Step 8.3: Refactor `Load(name)` to use it**

Replace the body of `PersistencyService::Load(const char *projectName)`
in `PersistencyService.cpp` with:
```cpp
PersistencyResult PersistencyService::Load(const char *projectName) {
  // (autosave / filename resolution unchanged)
  // ... keep the existing autosave + path construction logic ...
  // then:
  std::vector<uint8_t> buf(read file from FAT into buf);
  return LoadFromBuffer(buf.data(), buf.size());
}
```

**Important:** PersistencyDocument's child-iteration methods
(`FirstChild`/`NextSibling`/`NextAttribute`/`HasContent`) still use
`fp_->GetC()` (file reader). Refactor them to also support the
in-memory cursor, OR have `LoadFromBuffer` set up `fp_` to point at a
memory-backed `I_File*` instance.

The cleanest path: introduce `class MemoryFile : public I_File { ... }`
in `sources/Application/Persistency/` that reads from a buffer, and have
`LoadFromBuffer` do `fp_ = new MemoryFile(data, len);`. Then the
existing iteration methods work unchanged. This is a small
implementation detail, not a new abstraction — the `I_File` interface
already exists (`sources/System/FileSystem/I_File.h`).

If `I_File` proves too RP2040-coupled to host-compile, alternative:
add a `class ByteSource` (private to PersistencyDocument) with
`virtual int GetC()`, have a `FileByteSource` and `MemoryByteSource`
implementor, and have the iteration methods call `source_->GetC()`.
Then in production, `Load(filename)` constructs `FileByteSource` and
`LoadFromBuffer` constructs `MemoryByteSource` — same hot path, one
extra indirection at the file-vs-memory boundary. Codegen on RP2040
remains direct (the FileByteSource is the production case; the
MemoryByteSource only exists in host tests).

Either way, the **factory decision** is the implementer's; this plan
documents the two options and lets the implementer pick. The goal is:
- `Load(name)` reads file into a buffer, then calls `LoadFromBuffer(buf, len)`.
- `LoadFromBuffer` populates state identically to the previous `Load(name)`.
- No vtable cost added on RP2040 (the vtable indirection is on a path
  that runs once per project load, not in the audio path).

**Step 8.4: Build firmware**

```bash
cmake --build build --target application_instruments -j 4
cmake --build build-host --target picoTracker_persist -j 4
```
Expected: both build clean. If RP2040 firmware build complains about
the new code, surface it (per working agreement).

**Step 8.5: Run host tests**

```bash
./build-host/picoTracker_persist
```
Expected: all 4 tests pass.

**Step 8.6: Commit**

```bash
git add sources/Application/Persistency/
git commit -m "PersistencyService: route Load through LoadFromBuffer (zero-cost seam)"
```

---

## Task 9: Refactor `PersistencyService::Save` to use `SaveToBuffer`

**Files:**
- Modify: `sources/Application/Persistency/PersistencyService.cpp`

**Step 9.1: Refactor `SaveProjectData`**

Replace the file-write portion of `SaveProjectData(name, autosave)` with:
```cpp
  std::vector<uint8_t> buf(estimate + 256);  // see note
  size_t written = 0;
  Status s = SaveToBuffer(buf.data(), buf.size(), written);
  if (!s.ok()) {
    Trace::Error("PERSISTENCYSERVICE: SaveToBuffer failed");
    return PERSIST_ERROR;
  }
  // ... write buf[0..written] to FAT file ...
  // ... (autosave cleanup unchanged) ...
```

Initial buffer size: the lgpt_CTX2 fixture is 50 KB; a fresh project is
typically < 32 KB. Use `64 * 1024` as a safe default. If the buffer is
too small, `SaveToBuffer` returns `Status::ERROR`; caller should retry
with a larger buffer (out of scope for v1 — if the first project that
overflows 64 KB appears, the implementer adds grow-and-retry).

**Step 9.2: Build + run**

```bash
cmake --build build --target application_instruments -j 4
cmake --build build-host --target picoTracker_persist -j 4
./build-host/picoTracker_persist
```
Expected: all 4 tests pass, firmware still builds.

**Step 9.3: Commit**

```bash
git add sources/Application/Persistency/PersistencyService.cpp
git commit -m "PersistencyService: route Save through SaveToBuffer (zero-cost seam)"
```

---

## Task 10: Full firmware build verification

**Files:** none modified.

**Step 10.1: Full build**

```bash
cmake --build build -j 4 2>&1 | tail -20
```
Expected: clean. If anything fails, **stop and report** — it means the
refactor broke something on the production path that the host tests
didn't catch. (That's exactly the kind of bug the surface-refactor-bugs
memory is about.)

**Step 10.2: Run all host tests**

```bash
./build-host/picoTracker_persist
./build-host/picoTracker_tests
./build-host/synth_render --list
```
Expected: all pass. Report final assertion counts.

**Step 10.3: Commit (if any final tweaks)**

```bash
git add -A
git diff --cached --stat  # review
git commit -m "v1: host-testable persistence complete (4 doctest cases, 0 firmware regressions)"
```

---

## Self-review (plan vs. spec)

**Spec coverage:**
- [x] Round-trip default project (R1) — Task 4 + Task 5
- [x] Per-instrument NAMES round-trip, locking 9a1315c (R2) — Task 6
- [x] Real project file smoke, skip-if-missing (R3) — Task 7
- [x] Corrupted input guard (R4) — Task 2 + Task 3
- [x] `LoadFrom` / `SaveTo` API on the layer where the loop lives (PersistencyService, not Project) — Task 5 + Task 8 + Task 9
- [x] v2/v3 interface slots reserved (comment-only, no logic) — Task 8 closing comments
- [x] Zero runtime cost — the new code is non-virtual; the file/buffer abstraction is per-load, not per-audio-frame
- [x] `test_root/` git-ignored — done in commit `e766ebf5`
- [x] R3 fixture selection (lgpt_CTX2 = 2.3-Beta3) — Task 7

**Placeholder scan:** No "TBD", no "implement later". Each task has concrete code and an expected build/run output.

**Type consistency:** API names (`LoadFromBuffer`, `SaveToBuffer`, `Status::OK`/`Status::ERROR`) are introduced in one task and consumed in the next; no renames. `Status::ok()` may need adjustment to `Status::OK` per the actual enum shape in `System/io/Status.h` — flagged inline at Task 4.2.

**Open follow-ups (out of v1):**
- v2: instrument `rt::` namespace + AppWindow stub + InstrumentView event tests (see spec)
- v3: cross-version matrix, `test_tf/` committed small fixtures with `expected.json` (see spec)
