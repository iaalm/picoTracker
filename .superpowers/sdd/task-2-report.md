# Task 2 Report: R4 — corrupted input guard (RED)

## What was implemented

Replaced `tests/persist_tests.cpp` with the R4 test from the brief — a single test case that exercises `PersistencyDocument::LoadFromBuffer` with corrupted bytes:

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

## Compile failure captured (verbatim)

```
/home/simon/Projects/picoTracker/tests/persist_tests.cpp:7:13: error: 'class PersistencyDocument' has no member named 'LoadFromBuffer'
    7 |   CHECK(doc.LoadFromBuffer(garbage, sizeof(garbage)) == false);
```

Full build output excerpt:
```
gmake[3]: *** [CMakeFiles/picoTracker_persist.dir/build.make:93: CMakeFiles/picoTracker_persist.dir/persist_tests.cpp.o] Error 1
```

Exit code: 2. This is the expected RED — `LoadFromBuffer` does not yet exist on `PersistencyDocument`.

## Files changed

- `tests/persist_tests.cpp` — replaced smoke test with R4 test (8 lines added, 1 deleted)

## Self-review

- Only the R4 test was added — no other tests.
- Expected failure is captured: `LoadFromBuffer` not a member at `tests/persist_tests.cpp:7`.
- Commit message matches brief step 2.3 exactly.
- `PersistencyDocument.h` is at `sources/Application/Persistency/` (brief said `source/` singular — minor discrepancy, but the include path `Application/Persistency/PersistencyDocument.h` resolves correctly via cmake include paths).
- `HadError()` confirmed present at `PersistencyDocument.h:25`.
- Report file written and will be included in commit.