# Instrument Parameter API Redesign (Plan B)

**Status:** Draft for review
**Author:** KX1 workstream, June 2026
**Branch:** `kx1/plan-b-instrument-api` (off `origin/kx1`)
**Related:** `docs/synth-design-spec.md` (KX1 synth architecture)

---

## 1. Background and motivation

Every `I_Instrument` in picoTracker (Sample, MIDI, SID, OPAL, Synth) currently
stores its UI-facing parameters as a fixed set of `Variable` members. Each
`Variable` is a 32-byte polymorphic object with a vtable, FourCC id, type tag,
two value unions, a list pointer, a string pointer, and a list size. The
current KX1 SynthInstrument carries **35 such Variables** (≈ 1,184 B) plus a
40-slot pointer vector (160 B) for a per-instance footprint of ≈ 1,250 B
out of a 264 KB SRAM budget.

This is the wrong abstraction for a dense set of int-valued parameters:

1. The Variable class exists to support Observable semantics (UI field
   notification, dirty tracking, persistence) — but for instrument
   parameters all of that can be expressed by a 1-byte id, an int32 value,
   and a static metadata table.
2. With 16-32 sample/synth slots in the instrument pool, the Variable tax
   dominates the pool RAM cost. SampleInstrument at 16 instances eats
   ≈ 10.7 KB purely for parameter UI state.
3. The current `Variables()` list API forces a polymorphic, heap-iterating
   shape on what is conceptually a flat int array, which makes
   serialization, dirty tracking, and reset code path-coupled and easy to
   break silently.
4. The Variable abstraction was designed when parameters were 7-10 per
   instrument. KX1's 35 parameters and future dense param sets make the
   per-`Variable` overhead the dominant cost.

The goal of this redesign is to make the **packed int32 array** the source
of truth for instrument parameter values, with a **static `ParamSpec`
metadata table** describing each parameter, and to expose a single
**id-based, index-based API** on `I_Instrument` for UI, persistence, and
observability.

---

## 2. Goals and non-goals

### Goals

- Per-instance RAM for a 35-parameter instrument drops from ~1,250 B to
  ~150 B (~88% reduction).
- The `I_Instrument` interface expresses parameters in terms of `(id, idx,
  value, default, min, max, format)` — no `Variable*` exposed in the new
  API surface.
- A `ParamRef` adapter lets existing `UIField` subclasses bind to a
  parameter without knowing it is backed by packed storage or by legacy
  Variable storage.
- Migration is incremental: every PR leaves the codebase in a state that
  builds and runs correctly, with old and new paths coexisting until
  stage 8.
- Persistence is generic: a single `I_Instrument::SaveContent /
  RestoreContent / Purge` implementation handles every instrument type
  via the new API.
- The new API is **additive** in stage 0 — adding the new pure-virtual
  methods to `I_Instrument` is a compile-time break, but the patch in
  stage 0.5 makes all existing instruments immediately implement them via
  facade (zero behavioural change).

### Non-goals

- Changing the on-disk `.pti` file format. Persistence order changes are
  tolerated by name-based restore, but the XML element names stay
  identical.
- Removing the `Variable` class itself. It is still used by the
  instrument name, the sample field, the `SampleVariable` rendering
  controller, and (until stage 8) by the legacy instrument facades.
- Adding new instruments or new parameter semantics. This redesign is
  internal; user-visible behaviour is preserved.
- Modifying audio engine code (`SynthVoice::RenderBlock`,
  `SampleInstrument::Render`, etc.). Voice state and render parameters
  stay where they are.

---

## 3. New `I_Instrument` API

The new API is **pure virtual** in `I_Instrument`. Every concrete
instrument must implement it. Stage 0.5 introduces a default
implementation that adapts the existing `Variables()` to satisfy the new
methods, so the migration is staged.

```cpp
// sources/Application/Instruments/I_Instrument.h
class I_Instrument {
public:
  // --- Core ---
  virtual int GetParamCount() const;          // default impl in I_Instrument.cpp
  virtual FourCC GetParamID(int idx) const;
  virtual const char *GetParamName(int idx) const;
  virtual const char *GetParamFormat(int idx) const;

  // --- Range and stepping ---
  virtual int GetParamMin(int idx) const;
  virtual int GetParamMax(int idx) const;
  virtual int GetParamDefault(int idx) const;
  virtual int GetParamStep(int idx) const;
  virtual int GetParamBigStep(int idx) const;

  // --- Read / write ---
  virtual int GetParamValue(int idx) const;
  virtual void SetParamValue(int idx, int v);

  // --- State ---
  virtual bool IsParamModified(int idx) const;
  virtual void ResetParam(int idx);
  virtual void ResetAllParams() = 0;

  // --- Optional: variables list (legacy) ---
  // Returns nullptr once stage 8 deletes the legacy path.
  // Until then, instruments that have not migrated to packed storage
  // return their variables list here; migrated instruments return
  // nullptr.
  virtual etl::ivector<Variable *> *Variables() { return nullptr; }

  // --- Existing instrument interface (unchanged) ---
  virtual bool Init() = 0;
  virtual bool Start(int channel, unsigned char note, bool retrigger = true) = 0;
  virtual void Stop(int channel) = 0;
  virtual void OnStart() = 0;
  virtual bool Render(int channel, fixed *buffer, int size, bool updateTick) = 0;
  virtual bool IsInitialized() = 0;
  virtual bool IsEmpty() = 0;
  virtual InstrumentType GetType() = 0;
  virtual void ProcessCommand(int channel, FourCC cc, ushort value) = 0;
  virtual void Purge();
  // ... (TablePlayback, GetName, etc., unchanged)
};
```

### 3.1 `SetParamValue` notification

`SetParamValue(idx, v)` **must** trigger an observer notification if the
value actually changed. The base class provides the default
implementation:

```cpp
void I_Instrument::SetParamValue(int idx, int v) {
  if (GetParamValue(idx) == v) return;
  // Subclass writes the value to its storage.
  SetParamValueRaw(idx, v);   // protected helper, defined per subclass
  SetChanged();
  NotifyObservers();
}
```

This is the only observer notification in the new API. UI fields do not
hold their own `Variable` and do not subscribe to per-field change
events; they re-read the value at draw time, which already happens.

### 3.2 Index 0 reserved for instrument name

Every instrument exposes the **display name** as `idx == 0`. The
`GetParamName(0)` returns the string `"InstrumentName"`. The value is
the raw `etl::string<MAX_INSTRUMENT_NAME_LENGTH>` packed as int32 in the
first slot of the parameter array, or accessed through the existing
`name_` member for legacy facades.

This avoids having a separate "name" Variable and keeps the persistence
path uniform.

---

## 4. `ParamSpec` metadata table

A per-instrument-class static table describing each parameter. Lives in
flash on RP2040 (declared `static const`). The table is the **only**
source of metadata — no runtime allocation, no virtual calls.

```cpp
// sources/Application/Instruments/ParamSpec.h
#pragma once
#include <cstdint>
#include "Foundation/Types/Types.h"  // for FourCC

struct ParamSpec {
  FourCC  id;            // 1 B   (with 1 byte pad to align)
  uint8_t _pad0;
  uint16_t name_off;     // 2 B   offset into per-class name string table
  uint16_t format_off;   // 2 B   offset into per-class format string table
  uint8_t  default_;     // 1 B
  uint8_t  min;          // 1 B
  uint8_t  max;          // 1 B
  uint8_t  step;         // 1 B
  uint8_t  big_step;     // 1 B
  uint8_t  _pad1;        // 1 B   (alignment / future flags)
};
// 12 B per spec; 35 specs ≈ 420 B in flash
static_assert(sizeof(ParamSpec) == 12, "ParamSpec must be 12 B");
```

Each instrument class provides:

```cpp
class SynthInstrument : public I_Instrument {
public:
  // Packed parameter count including the name at idx 0.
  static constexpr int kParamCount = 36;   // 1 name + 35 synth params
  static const ParamSpec SPECS[kParamCount];
  static const char *const NAMES[];
  static const char *const FORMATS[];
  // ... rest of class
};
```

`NAMES` and `FORMATS` are themselves `static const` arrays of `const char*`
in flash, indexed by `ParamSpec::name_off` / `format_off`. Lookup is
`NAMES[SPECS[i].name_off]`.

The 12-byte per-spec cost is paid once per instrument class, not per
instance. On RP2040 with `static const` in C++, these go into `.rodata`
which is XIP'd from flash.

---

## 5. New `UIField` derived classes

The `UIField` family (`UIIntVarField`, `UIBigHexVarField`,
`UIIntVarOffField`, `UIBitmaskVarField`) currently binds to
`Variable&`. We add **a new set of parallel classes** that bind to
the new `I_Instrument` parameter API by `(instrument pointer, idx)`.
The existing `Variable&` constructors are **kept unchanged** so legacy
instruments (those still using Variable members) keep working without
modification to the field layer.

The new classes live alongside their `Variable&` counterparts in
`sources/Application/Views/BaseClasses/`:

```cpp
// sources/Application/Views/BaseClasses/UIParamIntVarField.h
class UIParamIntVarField : public UIIntVarField {
public:
  UIParamIntVarField(GUIPoint position, I_Instrument *instr, int idx,
                     const char *format, int min, int max, int step,
                     int bigStep, int offset = 0);
  // Inherits all of UIIntVarField's draw/input behaviour, but
  // overrides GetVariableID() to return instr_->GetParamID(idx),
  // and overrides GetVariable() / SetVariableValue() to call
  // instr_->GetParamValue(idx) / SetParamValue(idx, v).
};
```

The four new classes mirror the existing four:

| New class | Mirrors |
|---|---|
| `UIParamIntVarField` | `UIIntVarField` |
| `UIParamBigHexVarField` | `UIBigHexVarField` |
| `UIParamIntVarOffField` | `UIIntVarOffField` |
| `UIParamBitmaskVarField` | `UIBitmaskVarField` |

Each is a thin override of the corresponding `UIField` subclass. The
internal storage is `(I_Instrument *instr_; int idx_;)` instead of
`(Variable &v_)`. The drawing, scrolling, and input-handling logic
in the base classes is reused.

### 5.1 Lifetime

The new classes are short-lived. `InstrumentView::fillXxxParameters`
constructs them during `refreshInstrumentFields`, they are stored by
value in `fieldList_`, and they are dropped when the field list is
cleared (which happens on every instrument type change). **They do
not outlive the instrument edit session**, so the lifetime is bounded
by the field view's rebuild cycle, which already drops all field
references on instrument change.

### 5.2 Why a new class rather than a `ParamRef` adapter

The original draft proposed a `ParamRef` adapter and an additional
constructor overload on the existing `UIField` classes. The user
chose the new-class approach for two reasons:

1. **Cleaner type identity.** `UIParamIntVarField` is unambiguously
   "a field bound to the new API". A `ParamRef` overload of
   `UIIntVarField` would mix two storage models under one class
   name, which makes static analysis and grep harder.
2. **Easier migration of the field layer later.** If the legacy
   `Variable&` paths are eventually removed (stage 8), the
   `UIIntVarField` class shrinks to a single set of constructors,
   and the `UIParam*` classes become the only path. Splitting
   classes now makes that final cut cleaner.

The implementation cost is roughly the same as the `ParamRef`
approach — the new classes are mostly inheritance.

---

## 6. Persistence

`I_Instrument::SaveContent` and `RestoreContent` are rewritten to use
the new API. `Purge` is rewritten to call `ResetAllParams`.

```cpp
void I_Instrument::SaveContent(tinyxml2::XMLPrinter *p) {
  p->PushAttribute("VERSION", PROJECT_NUMBER);
  p->PushAttribute("TYPE", InstrumentTypeNames[GetType()]);
  for (int i = 0; i < GetParamCount(); i++) {
    p->OpenElement("PARAM");
    p->PushAttribute("NAME", GetParamName(i));
    char buf[16];
    npf_snprintf(buf, sizeof(buf), GetParamFormat(i), GetParamValue(i));
    p->PushAttribute("VALUE", buf);
    p->CloseElement();
  }
}

void I_Instrument::RestoreContent(PersistencyDocument *doc) {
  // Walk PARAM children. For each, look up by NAME in our spec table
  // and call SetParamValue(idx, parsed_value).
  bool sub = doc->FirstChild();
  while (sub) {
    char name[64] = "";
    char value[64] = "";
    bool hasAttr = doc->NextAttribute();
    while (hasAttr) {
      if (!strcasecmp(doc->attrname_, "NAME"))  strcpy(name,  doc->attrval_);
      if (!strcasecmp(doc->attrname_, "VALUE")) strcpy(value, doc->attrval_);
      hasAttr = doc->NextAttribute();
    }
    if (name[0] && value[0]) {
      int idx = FindParamByName(name);
      if (idx >= 0) {
        SetParamValue(idx, ParseParamValue(idx, value));
      } else {
        Trace::Error("Unknown param '%s' on %s instrument",
                     name, InstrumentTypeNames[GetType()]);
      }
    }
    sub = doc->NextSibling();
  }
}

void I_Instrument::Purge() {
  ResetAllParams();
}
```

`FindParamByName` is a linear scan over `GetParamCount()` parameters.
For ≤ 36 params this is fine and avoids a hash table.

### 6.1 Old project file compatibility

`.pti` files saved before this redesign persist `NAME="InstrumentName"`
as the first PARAM. Old `SaveContent` and `RestoreContent` already
agree on this. The redesign keeps the name at `idx == 0` so old files
restore correctly. If a future change reorders parameters, restore-by-
name continues to work because lookup is name-based, not index-based.

### 6.2 Format-string compatibility

`GetParamFormat(i)` returns the printf template that the existing
`UIField` would have used for that parameter. Persistence writes the
formatted string (same as before). On restore, the format is consulted
in reverse (or `sscanf`) to parse back to int. Most parameters are
decimal or hex; `SetParamValue` accepts the int.

For the boolean-typed parameters (`hardSync`, `ringMod`,
`tableAutoAutomation`), the format is `"%s"` and the value is "on" or
"off". `ParseParamValue` handles this string-to-int conversion.

---

## 7. Per-instrument migration

The table below summarizes the per-instrument work to migrate from
`Variable` storage to packed `int32_t[ParamCount]` storage.

| Instrument | Current params | Target per-instance | Per-instance saving | Pool saving @16 |
|---|---|---|---|---|
| SynthInstrument | 35 Variable + 40-vec | `int32_t[36]` (144 B) | ~1,100 B | 17.6 KB @ 16 |
| SampleInstrument | 21 Variable + 21-vec | `int32_t[22]` (88 B) | ~610 B | 9.8 KB @ 16 |
| MidiInstrument | 7 Variable + 7-vec | `int32_t[8]` (32 B) | ~300 B | 4.8 KB @ 16 |
| SIDInstrument | 19 Variable + 19-vec + `cRSID` static | `int32_t[20]` (80 B) | ~320 B | 1.0 KB @ 3 |
| OpalInstrument | 16 Variable + 16-vec + `Opal` DSP embed | `int32_t[17]` (68 B) | ~730 B | 2.2 KB @ 3 |

### 7.1 Special cases

- **SynthInstrument** — the per-channel voice state (`SynthVoice
  voices_[SONG_CHANNEL_COUNT]`) is unaffected. `buildParams` reads
  from the packed array instead of from `Variable` members. Lookup
  table arrays (`sineTable[1024]`, etc.) are unchanged but should be
  made `const` in a separate PR to push them to flash.
- **SampleInstrument** — `slicePoints_[16]` (64 B) stays as a separate
  field; it is not a UI parameter. The `SampleVariable sample_`
  rendering controller stays as `Variable` (its rendering callback
  path is not part of this redesign). The **sample selection field**
  (`SampleInstrumentSample` FourCC) **stays as a legacy `Variable`**;
  it has a custom UI rendering path (`%.17s` format with sample name
  lookup) and a custom persistence path. Migrating it is a separate
  task deferred past stage 8.
- **SIDInstrument** — the static `cRSID sid1_/sid2_` chip emulator
  state is unaffected. Per-instance parameters are independent of the
  chip state.
- **OpalInstrument** — the embedded `Opal` DSP class (1 channel + 2
  operators per picoTracker MOD) is unaffected. The instrument's UI
  parameters are decoupled from the DSP state.

### 7.2 Pool capacity after migration

With the redesign complete, the following pool capacities are within
the original 264 KB SRAM budget:

| Instrument | Old max | New max (master Pico) | New max (Advance) |
|---|---|---|---|
| Synth | 8 | 16 | 32 |
| Sample | 16 | 32 | 64 |
| MIDI | 16 | 32 | 64 |
| SID | 3 | 0 (dropped, see §9) | 3 |
| OPAL | 3 | 0 (dropped, see §9) | 3 |

The Advance numbers assume the Advance has its own budget headroom
per `4716fa1b` ("Adjust number of resources for Advance").

---

## 8. Migration stages

Each stage is a mergeable PR. The codebase compiles and runs at the
end of every stage.

### Stage 0 — Add the new API surface

- Modify `I_Instrument.h` to add the new method declarations.
- Provide default implementations in `I_Instrument.cpp` that adapt
  the legacy `Variables()` list to the new index-based API.
- Methods are **non-pure-virtual** in stage 0 so the build stays
  green. They become `= 0` only in stage 7.
- Every existing concrete instrument inherits the defaults and
  works unchanged.

### Stage 0.5 — Facade the new API on legacy instruments

- Add the new method implementations to `SampleInstrument`,
  `MidiInstrument`, `SIDInstrument`, `OpalInstrument`. Each
  implementation reads from the existing `Variable` members (no
  storage change). The facade is a thin shim.
- The codebase compiles. Behaviour is unchanged.

### Stage 0.6 — Add the new `UIParam*` field classes

- New `UIParamIntVarField`, `UIParamBigHexVarField`,
  `UIParamIntVarOffField`, `UIParamBitmaskVarField` classes. Each
  is a thin subclass of the corresponding existing `UIField` class
  that overrides `GetVariableID()`, `GetVariable()` and
  `SetVariableValue()` to delegate to the new `I_Instrument` API.
- Old `Variable&` constructor paths still work; existing
  `fillXxxParameters` callsites that bind to legacy instruments
  are not touched.

### Stage 0.7 — Switch persistence to new API

- Rewrite `I_Instrument::SaveContent`, `RestoreContent`, `Purge` to
  use the new API.
- All instruments work via the new persistence path even though
  storage is still `Variable`-based.

### Stage 0.8 — Verify legacy path

- Full instrument-switching regression. Save a project, reload it,
  confirm round-trip. Run a project on each instrument type and
  confirm the UI behaves identically to the pre-stage-0 build.

### Stage 1 — Migrate `SIDInstrument` (first per user decision)

- Replace `SIDInstrument`'s 19 `Variable` members with
  `int32_t params_[20]` (1 name + 19 params).
- Implement the new API directly on the packed array.
- Add `SPECS`, `NAMES`, `FORMATS` static tables.
- Update `InstrumentView::fillSIDParameters` to use the new
  `UIParam*` classes.
- Update `SIDInstrument::Start` / `ProcessCommand` to read from the
  packed array via index.
- The static `cRSID sid1_/sid2_` chip emulator state is **not**
  touched (it lives outside the parameter API).
- Measure `sizeof(SIDInstrument)` before and after. Confirm
  `etl::pool<SIDInstrument, 3>` savings.
- This is the **first real migration** — the validation case that
  proves the pattern works end-to-end.

### Stage 2 — Migrate `OpalInstrument`

- Replace 16 `Variable` members with `int32_t params_[17]`.
- Migrate `fillOpalParameters` to the new `UIParam*` classes.
- The embedded `Opal` DSP class is **not** touched; only the
  instrument parameter layer changes.
- Measure `sizeof` delta.

### Stage 3 — Migrate `SynthInstrument`

- Replace 35 `Variable` members with `int32_t params_[36]`.
- Update `SynthInstrument::buildParams` to read from the packed
  array via index. The mapping (`params_[0]` → `p.algorithm`,
  `params_[1]` → `p.feedback`, ...) is a fixed table written
  inline.
- Update `fillSynthParameters` to use the new `UIParam*` classes.
- Measure `sizeof` delta. This is the largest single pool saving.

### Stage 4 — Migrate `SampleInstrument`

- Replace 20 of the 21 `Variable` members with `int32_t params_[21]`
  (1 name + 20 packed params). The 21st variable is
  `SampleInstrumentSample` (the sample-selection field), which
  stays as a legacy `Variable` per §9.4.
- `slicePoints_[16]` (64 B) stays as a separate field outside the
  packed array.
- `SampleVariable sample_` rendering controller stays as a
  `Variable`.
- Update `fillSampleParameters` to use the new `UIParam*` classes
  for the 20 migrated parameters; the sample field continues to
  use `UIIntVarField` with the legacy `Variable&`.
- Measure `sizeof` delta.

### Stage 5 — Migrate `MidiInstrument`

- Replace 7 `Variable` members with `int32_t params_[8]`.
- Update `fillMidiParameters` to use the new `UIParam*` classes.
- Measure `sizeof` delta.

### Stage 6 — Final regression on Pico and Advance firmware

- Full instrument-switching test for every type: SID, OPAL,
  Synth, Sample, MIDI, None.
- Persistence round-trip on every type.
- Pool capacity tests at the planned new limits.
- Per-channel voice state tests (KX1 with all 8 channels active).

### Stage 7 — Delete legacy `Variables()` and `VariableContainer`

- `I_Instrument::Variables()` returns `nullptr` always.
- `VariableContainer` base is removed from `I_Instrument`.
- `findInstrumentVariable` and friends that iterate `Variables()`
  are deleted.
- Legacy `Variable*` overloads in `UIField` are deleted; only the
  new `UIParam*` classes remain.
- The `Variable` class itself is **not** removed — it is still used
  by the `SampleVariable` rendering controller and by the
  instrument name plumbing in the legacy path.

### Stage 8 — Documentation and final pass

- Update `docs/DEV.md` to describe the parameter model.
- Update `usermanual/` for any user-visible changes (none expected).
- Full regression on Pico and Advance firmware.
- Tag the migration as complete.

---

## 9. Cross-cutting decisions

### 9.1 SID and OPAL are kept and migrated first

Per user decision, SID and OPAL are not retired. They are migrated
to the new param API **first** (stages 1 and 2) — the rationale is
that they are the simplest real instruments with non-trivial
parameter sets, so they validate the migration pattern before the
larger Sample and Synth migrations. SID/OPAL continue to exist in
the master and Advance builds with their existing pool sizes
(`MAX_SIDINSTRUMENT_COUNT = 0x03`, `MAX_OPALINSTRUMENT_COUNT = 0x03`).

### 9.2 KX1 lookup table `const` fix

`SynthVoice.cpp` declares its lookup tables (`sineTable[1024]`,
`noteInc[128]`, `envRate[16]`, `cutoffCoef[256]`, `resoCoef[16]`) as
file-scope globals without `const`. They occupy ~3.6 KB of `.data`
that should be `.rodata` (flash). **This is a separate, single-line
fix that should land before or alongside stage 1**, independent of
the param API work.

### 9.3 Soundfont

Soundfont support was removed in `6194311e` (2023). No revival
planned. Not affected by this redesign.

### 9.4 Sample field migration (deferred)

The `SampleInstrumentSample` parameter (the field that selects which
sample in the pool to play) is a special case: it has a custom UI
renderer (sample name lookup via the `SamplePool` name list) and
custom persistence. Migrating it cleanly requires a richer
`GetParamFormat` API and possibly a callback for name lookup. It
remains as a legacy `Variable` until a follow-up redesign. The
packed-storage migration for the other 20 SampleInstrument parameters
proceeds without it.

---

## 10. RAM budget delta (summary)

Pre-redesign (master, RP2040):
- Sample pool: 16 × ~700 B = ~10.7 KB
- Synth pool: 8 × ~1,250 B = ~10.0 KB
- MIDI pool: 16 × ~330 B = ~5.3 KB
- SID pool: 3 × ~400 B = ~1.2 KB (plus ~0.6 KB cRSID static)
- OPAL pool: 3 × ~800 B = ~2.4 KB
- **Total instrument param RAM: ~30 KB**

Post-redesign (master, RP2040), pool sizes unchanged:
- Sample pool: 16 × 88 B = 1.4 KB
- Synth pool: 8 × 144 B = 1.2 KB
- MIDI pool: 16 × 32 B = 0.5 KB
- SID pool: 3 × 80 B = 0.24 KB
- OPAL pool: 3 × 68 B = 0.2 KB
- **Total instrument param RAM: ~3.5 KB**

**Net delta: ≈ 26.5 KB reclaimed.**

This headroom can be spent on:
- Raising `MAX_SAMPLEINSTRUMENT_COUNT` to 32 (+ 1.4 KB)
- Raising `MAX_SYNTHINSTRUMENT_COUNT` to 32 (+ 3.5 KB)
- Doubling `SONG_ROW_COUNT` (Phrase 128 → 256) (+ 16.6 KB)
- Adding audio effect modules (reverb/delay ~ 4-8 KB each)
- Larger event queue or other UX buffers

---

## 11. Risks and mitigations

| Risk | Probability | Impact | Mitigation |
|---|---|---|---|
| `UIParam*` field outlives the instrument (UI field holds a stale instrument pointer / idx) | Medium | High | `InstrumentView::refreshInstrumentFields` already clears `fieldList_` on instrument change. The new field bindings are dropped with the field list. |
| Persistence regression on old `.pti` files | Low | High | Restore is name-based, not index-based. Reordering the spec table is safe. Format strings match the old ones. |
| SetParamValue not triggering UI redraw | Medium | Medium | The new base-class `SetParamValue` calls `NotifyObservers` on change. UI fields re-read on the next draw. Already tested pattern in `Observable`. |
| `SampleInstrument` slice points and `SampleVariable` accidentally moved into packed array | Low | High | Slice points stay as a separate field; `SampleVariable` stays as a `Variable`. Migration script enumerates which `Variable` members move; the rest are annotated as "non-migrated". |
| `Variable` vtable pointer cost for instruments that keep some legacy Variables | Low | Low | Each remaining `Variable` is 32 B. With 1-2 per non-migrated instrument, the cost is trivial (~100 B per instrument). |
| Build time growth from large spec tables | Low | Low | Static `const` arrays of `const char*` are not heavy. No new code generation beyond existing facades. |
| Review fatigue: too many small migration PRs | Medium | Medium | Stage ordering: high-pool-saving instruments (Synth, Sample) go first, so reviewers see the wins early. |

---

## 12. Decisions

All open questions resolved:

1. **Old `Variables()` interface** — kept until **stage 7** (when the
   legacy `Variable&` UI fields are deleted). The dual-path
   complexity is acceptable for the migration period.
2. **UIField design** — **new derived classes** (`UIParamIntVarField`,
   `UIParamBigHexVarField`, `UIParamIntVarOffField`,
   `UIParamBitmaskVarField`). Cleaner type identity and easier
   final cut in stage 7.
3. **Name at idx 0** — **yes**, the instrument name is the first
   parameter in every instrument's spec table. Persistence
   round-trips old `.pti` files correctly.
4. **Sample field** — **deferred** to a follow-up redesign. The
   `SampleInstrumentSample` parameter stays as a legacy `Variable`
   through stage 7. The other 20 SampleInstrument parameters are
   migrated in stage 4.
5. **SID/OPAL** — **migrated, kept**, and **first** in the order.
   They validate the migration pattern before Sample and Synth
   because their parameter sets are small but non-trivial.

---

## 13. Timeline (estimate)

| Stages | Description | Duration | Cumulative |
|---|---|---|---|
| 0–0.8 | New API + facades + UI + persistence switch | 1.5 weeks | 1.5 weeks |
| 1 | SID migration + first RAM numbers | 1 week | 2.5 weeks |
| 2 | OPAL migration | 1 week | 3.5 weeks |
| 3 | Synth migration | 1 week | 4.5 weeks |
| 4 | Sample migration | 1 week | 5.5 weeks |
| 5 | MIDI migration | 0.5 week | 6 weeks |
| 6 | Final regression on Advance and Pico | 0.5 week | 6.5 weeks |
| 7 | Legacy path deletion (no `Variables()`, no `Variable&` UIFields) | 1 week | 7.5 weeks |
| 8 | Documentation, final pass | 0.5 week | 8 weeks |

**Total: ≈ 8 weeks for the full migration, deliverable as 8-9
mergeable PRs.**

If the team only wants the first 4 stages (SID, OPAL, Synth,
Sample — the highest-value cases), the critical path is stages
0-4 plus verification: **≈ 5.5 weeks** to the first major checkpoint
where ≈ 27 KB is reclaimed.
