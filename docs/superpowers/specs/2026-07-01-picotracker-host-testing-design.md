# picoTracker Host Testing — Design Spec

**Date:** 2026-07-01
**Branch:** `kx1/plan-b-instrument-api`
**Status:** Draft — pending user review

---

## Motivation

The recent `stage 0–5` instrument-API migration (≈3500 cross-cutting lines on
`kx1/plan-b-instrument-api`) shipped at least one silent-failure bug
(`9a1315c` — `NAMES` table spelled with C++ identifiers instead of legacy
FourCC `c_str()` values, so every `PARAM` in a saved `.pti` file was silently
dropped on load). The host test suite at the time contained exactly two
low-level test files (`foundation_types_tests.cpp`, `wavheader_tests.cpp`) —
no coverage of the migration code path. Every regression of this class will
continue to escape into firmware until the persistence layer is host-testable.

Goal: make the persistence path and the view-state path host-testable with
**zero runtime overhead** on RP2040, so the next migration regression class
fails `ctest` instead of failing a firmware flash.

## Non-goals

- Testing DSP / audio path. `synth_render` already covers this and is not the
  source of recent regressions.
- Replacing the on-device test rig for hardware-specific behaviour (display,
  battery, button debounce).
- Cross-version `.pti` migration (legacy `0.50`/`1.0` files loaded by new
  firmware). Deferred to v3 — the fixture inventory is now in place
  (`test_root/projects/`), the load interface supports adding it, but the
  version-dispatch logic is out of scope for v1.

## Constraints

- **Zero runtime cost on RP2040.** Audio path is RAM-bound at 92% utilisation
  (`sources/Services/Audio/AudioDriver.h:16-18`); any abstraction that adds
  a vtable or function-pointer indirection to a hot path is rejected.
- **Minimal production-code surface change.** The migration is not yet
  merged; refactors to `Project` / `PersistencyService` must be small enough
  to review in one PR.
- **No new build dependencies in production.** `tinyxml2`, `yxml`, `etl` are
  already linked; host tests reuse them as-is.

## Approach

Three test seams, each **zero-cost** — implemented with the cheapest C++
mechanism that achieves compile-time selection and runtime equivalence:

1. **Persistence: function-signature change.** `Project::Load(const char*)`
   becomes `Project::LoadFrom(const uint8_t* data, size_t len)`. Production
   code reads the FAT file into a buffer then calls `LoadFrom`. Test code
   passes `std::vector<uint8_t>` directly. **No vtable, no interface, no
   abstraction** — just a function taking bytes instead of a path.
2. **Render: link-time namespace swap.** Introduce a header `sources/UIFramework/RenderTarget.h`
   that declares `namespace rt { void putChar(int,int,char); ... }`. The
   production build links `chargfx_render.cpp` (one-line wrappers). The host
   test build links `tests/stubs/rt_test.cpp` (writes to `char screen[24][32]`).
   Every render callsite becomes `rt::putChar(...)` — a direct, link-resolved
   function call with identical codegen to the previous `chargfx::print_char`.
3. **Events: no abstraction needed.** `view->onEvent(Event)` is already a
   plain method; tests construct synthetic `Event` objects and call it.

> **Why not a HAL?** A single `IHal` interface would add vtable dispatch to
> every render/log/event call and pull the refactor into every subsystem
> (EventManager, Logger, storage, time). The current bug class (string
> mismatch, view state, load path) does not need it. Each seam is opened
> exactly where needed, no further.

## Architecture (stage 1 — persistence only)

### Production code changes

`sources/Application/Model/Project.h`:
```cpp
class Project {
  // ...existing API...
  Status LoadFrom(const uint8_t* data, size_t len);   // NEW
  Status SaveTo  (uint8_t*       data, size_t cap,
                  size_t&        written);           // NEW
  // Keep Load(const char* name) for now — converted to a thin FAT wrapper
  // that reads the file then calls LoadFrom. Delete after v2.
};
```

`sources/Application/Model/Project.cpp`:
- `LoadFrom` does the parsing `Load` does today (constructs an XML document
  from bytes via tinyxml2, populates Song / Phrase / InstrumentBank /
  WatchedVariables).
- `SaveTo` writes XML to the caller's buffer.
- `Load(const char*)` and `Save(...)` in `PersistencyService` are reduced
  to "read file bytes; call `LoadFrom`" / "call `SaveTo`; write file
  bytes" — about 5 lines each.

### Test binary changes

`tests/CMakeLists.txt`:
```cmake
add_executable(picoTracker_persist
  test_main.cpp
  persist_tests.cpp
  ../sources/Application/Model/Project.cpp
  ../sources/Application/Model/Song.cpp
  ../sources/Application/Model/Phrase.cpp
  ../sources/Application/Model/Groove.cpp
  ../sources/Application/Model/Chain.cpp
  ../sources/Application/Model/Table.cpp
  ../sources/Application/Model/Config.cpp
  ../sources/Application/Model/Scale.cpp
  ../sources/Application/Persistency/Persistent.cpp
  ../sources/Application/Instruments/I_Instrument.cpp
  ../sources/Application/Instruments/InstrumentBank.cpp
  ../sources/Application/Instruments/SampleInstrument.cpp
  ../sources/Application/Instruments/SynthInstrument.cpp
  ../sources/Application/Instruments/MidiInstrument.cpp
  ../sources/Application/Instruments/OpalInstrument.cpp
  ../sources/Application/Instruments/SIDInstrument.cpp
  ../sources/Application/Instruments/MacroInstrument.cpp
  ../sources/Application/Instruments/NoneInstrument.cpp
  ../sources/Application/Instruments/CommandList.cpp
  ../sources/Application/Instruments/InstrumentNameVariable.cpp
  ../sources/Application/Instruments/SampleVariable.cpp
  ../sources/Application/Instruments/SampleInstrumentDatas.h
  # tinyxml2 / etl come via existing INCLUDE path
)
```
Header-only `SampleInstrumentDatas.h` is included transitively; no `.cpp`
needed for pure-data headers.

## Testing strategy (stage 1)

Four test cases, all host-only, no hardware:

| ID | What it catches | Assertion |
|---|---|---|
| **R1** Round-trip default project | Self-save-self-load regressions | `SaveTo` buffer round-trips through `LoadFrom` with byte-stable length and identical Project state |
| **R2** Per-instrument round-trip | The `9a1315c` NAMES bug; legacy FourCC mismatches | For each of `{Sample,Synth,Opal,SID,MIDI,Macro}` × `GetParamCount()`, set a non-default value via `SetParamValue(idx, ...)`, round-trip, assert `GetParamValue(idx)` matches. Locks down the exact surface that broke. |
| **R3** Real-project smoke | "Loading a real `.pti` breaks" / version-string drift | Open `test_root/projects/lgpt_CTX2/lgptsav.dat` (currently the only project at the firmware's version `2.3-Beta3`), call `LoadFrom`, assert status `OK`, assert `GetProjectName()` is non-empty, assert instrument count > 0, assert no exception |
| **R4** Corrupted-input guard | Parser crash / silent-default-on-error regression | Feed `{0xDE,0xAD,0xBE,0xEF}`; assert `LoadFrom` returns error status — no crash, no silent default |

### R3 fixture access

`test_root/` is git-ignored (added `.gitignore` line 47 in this PR).
The fixture is read by absolute path during test runs; if
`test_root/projects/lgpt_CTX2/lgptsav.dat` is missing, R3 is **skipped with
a clear message**, not failed — so CI without the data still runs R1/R2/R4.

### R2 NAMES coverage scope

R2 covers the param name spaces touched by the `stage 0–5` migration
(`SID/OPAL/Synth/Sample/MIDI`), not the entire `NAMES` table. Reasoning:
the recent bug was in this exact surface; full-table coverage would
multiply test runtime with no additional regression value today.

## Deferred (interface reserved, logic not implemented in v1)

- **`v2` — view state + render snapshot.** Adds `namespace rt` (and the two
  `.cpp` files). De-risks the AppWindow-singleton coupling with a
  `TestAppWindow` subclass that maps `DrawString/SetColor/Clear` to
  `rt::putChar`. Adds 1–2 InstrumentView tests driving synthetic events.
- **`v3` — cross-version matrix.** `LoadFrom` keeps a `uint32_t fileVersion`
  parameter slot (unused in v1 — the implementation always passes
  `PROJECT_VERSION`). Once added, the 11 historical projects in
  `test_root/projects/` can be loaded and asserted against per-version
  expected-state JSON. Separate `test_tf/` directory will hold the small,
  curated `expected.json` fixtures (≤ a few KB each) that ship in git;
  `test_root/` keeps the larger raw `.pti` files locally only.

## Why this approach over alternatives considered

- **Full HAL (`IHal`)**: rejected — vtable cost on hot path, refactor
  surface much larger than needed for current bug class. The bug we just
  fixed (NAMES mismatch) is a string lookup, not a HAL-amenable issue.
- **`HOST_TEST` ifdef everywhere**: rejected — pollutes production code,
  fights the existing `tests/stubs/` pattern, hard to grow incrementally.
- **One-step "ship the full view test suite"**: rejected — Phase 1+2
  doubles the work, defers any value, and `AppWindow` is the heaviest
  piece to decouple (placement-new singleton, EventManager + SysMutex
  deps, char-screen globals). Deserves its own review.

## Open questions

- **R3 fixture permanence.** Currently `test_root/` is local-only (git-ignored).
  Long-term, should `lgpt_CTX2/lgptsav.dat` (~50 KB) move to a committed
  `tests/fixtures/` so CI without `test_root/` runs R3? Deferred — current
  "skip when missing" behaviour is acceptable for v1.
- **`test_tf/` directory.** User-requested long-term home for small test
  fixtures with committed `expected.json`. **Not** part of v1 — the v3
  matrix is the trigger for this directory.
- **Where the Project `LoadFrom` API lives.** Spec assumes it stays on
  `Project` class. Alternative: put it on `PersistencyService` and make
  `Project` agnostic. Deferred to design review — implementation will pick
  whichever is shorter once the code is read in full.

## Self-review checklist

- [x] No placeholders / TODOs in concrete deliverables
- [x] API signatures consistent across production + test
- [x] Scope is one implementation cycle (≈ 1 week, single PR)
- [x] v2/v3 boundaries explicit, not implicit
- [x] No ambiguous requirements: every test has a stated assertion and a
      stated expected failure mode
