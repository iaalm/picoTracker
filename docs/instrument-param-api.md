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
  virtual int GetParamCount() const = 0;
  virtual FourCC GetParamID(int idx) const = 0;
  virtual const char *GetParamName(int idx) const = 0;
  virtual const char *GetParamFormat(int idx) const = 0;

  // --- Range and stepping ---
  virtual int GetParamMin(int idx) const = 0;
  virtual int GetParamMax(int idx) const = 0;
  virtual int GetParamDefault(int idx) const = 0;
  virtual int GetParamStep(int idx) const = 0;
  virtual int GetParamBigStep(int idx) const = 0;

  // --- Read / write ---
  virtual int GetParamValue(int idx) const = 0;
  virtual void SetParamValue(int idx, int v) = 0;

  // --- State ---
  virtual bool IsParamModified(int idx) const = 0;
  virtual void ResetParam(int idx) = 0;
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

## 5. `ParamRef` UI adapter

The `UIField` family (`UIIntVarField`, `UIBigHexVarField`,
`UIIntVarOffField`, `UIBitmaskVarField`) currently binds to `Variable&`.
We add a parallel binding to a `ParamRef` without changing the
existing `Variable&` constructors.

```cpp
// sources/Foundation/Variables/ParamRef.h
#pragma once
#include "Application/Instruments/I_Instrument.h"

class ParamRef {
  I_Instrument *instr_;
  int idx_;
public:
  constexpr ParamRef(I_Instrument *instr, int idx) : instr_(instr), idx_(idx) {}

  int  GetInt() const    { return instr_->GetParamValue(idx_); }
  void SetInt(int v)     { instr_->SetParamValue(idx_, v); }
  bool GetBool() const   { return GetInt() != 0; }
  void SetBool(bool b)   { SetInt(b ? 1 : 0); }
  bool IsModified() const { return instr_->IsParamModified(idx_); }
  void Reset()           { instr_->ResetParam(idx_); }
  FourCC GetID() const   { return instr_->GetParamID(idx_); }
};
```

`UIField` subclasses gain a constructor overload that takes a
`ParamRef` in place of the `Variable&`. They store the `ParamRef` by
value (16 B on RP2040: 4 B pointer + 4 B idx + alignment). At draw
time, they call `paramRef_.GetInt()`; at user-input time, they call
`paramRef_.SetInt(v)`.

A `ParamRef` is short-lived. It is constructed by
`InstrumentView::fillXxxParameters` during a `refreshInstrumentFields`
call, stored by value in the UIField, and discarded when the
`fieldList_` is cleared. **It does not outlive the instrument edit
session**, so the lifetime is bounded by the field view's rebuild
cycle, which already drops all field references on instrument change.

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

- Modify `I_Instrument.h` to add the new pure-virtual method
  declarations.
- Every existing concrete instrument will fail to compile.

### Stage 0.5 — Facade the new API on legacy instruments

- Add the new method implementations to `SampleInstrument`,
  `MidiInstrument`, `SIDInstrument`, `OpalInstrument`. Each
  implementation reads from the existing `Variable` members (no
  storage change). The facade is a thin shim.
- The codebase compiles. Behaviour is unchanged.

### Stage 0.6 — Add `ParamRef` and `UIField` overloads

- New `ParamRef` class.
- `UIIntVarField`, `UIBigHexVarField`, `UIIntVarOffField`,
  `UIBitmaskVarField` each gain a `ParamRef` constructor overload.
- Old `Variable&` constructor paths still work.

### Stage 0.7 — Switch persistence to new API

- Rewrite `I_Instrument::SaveContent`, `RestoreContent`, `Purge` to
  use the new API.
- All instruments work via the new persistence path even though
  storage is still `Variable`-based.

### Stage 0.8 — Verify legacy path

- Full instrument-switching regression. Save a project, reload it,
  confirm round-trip. Run a project on each instrument type and
  confirm the UI behaves identically to the pre-stage-0 build.

### Stage 1 — Migrate `SynthInstrument`

- Replace 35 `Variable` members with `int32_t params_[36]`.
- Implement new API directly on the packed array.
- Update `SynthInstrument::buildParams` to read from the packed
  array.
- Update `InstrumentView::fillSynthParameters` to use `ParamRef`
  bindings.
- Measure `sizeof(SynthInstrument)` before and after. Confirm
  `etl::pool<SynthInstrument, N>` savings.
- This is the **first deliverable** for the user: real RAM numbers.

### Stages 2-7 — Migrate other instruments

One instrument per stage. Each stage:
- Replace `Variable` members with packed array
- Add `SPECS` / `NAMES` / `FORMATS` tables
- Update `InstrumentView::fillXxxParameters` to use `ParamRef`
- Add a focused unit test or `synth_render` regression
- Update PR notes with `sizeof` deltas

Order: Sample (highest pool savings), MIDI (simplest), SID
(shared state care), OPAL (embedded DSP care).

### Stage 8 — Delete legacy `Variables()` and `VariableContainer`

- `I_Instrument::Variables()` returns `nullptr` always.
- `VariableContainer` base is removed from `I_Instrument`.
- `findInstrumentVariable` and friends that iterate `Variables()`
  are deleted.
- Legacy `Variable*` overloads in `UIField` are deleted; only the
  `ParamRef` overloads remain.

### Stage 9 — Documentation and final regression

- Update `docs/DEV.md` to describe the parameter model.
- Update `usermanual/` for any user-visible changes (none expected).
- Full regression on Pico and Advance firmware.

---

## 9. Cross-cutting decisions

### 9.1 SID and OPAL retirement

Per the prior audit and discussion, the Advance branch already raises
`MAX_SYNTHINSTRUMENT_COUNT` to 8 with SID/OPAL still at 3. This
redesign is **agnostic** to whether SID/OPAL stay: their migration
follows the same path. If the team chooses to drop SID/OPAL entirely
as part of the broader product direction, the corresponding stages are
simply skipped.

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
| `ParamRef` outlives the instrument (UI field holds a stale ref) | Medium | High | `InstrumentView::refreshInstrumentFields` already clears `fieldList_` on instrument change. The new `ParamRef` binding is dropped with the fields. |
| Persistence regression on old `.pti` files | Low | High | Restore is name-based, not index-based. Reordering the spec table is safe. Format strings match the old ones. |
| SetParamValue not triggering UI redraw | Medium | Medium | The new base-class `SetParamValue` calls `NotifyObservers` on change. UI fields re-read on the next draw. Already tested pattern in `Observable`. |
| `SampleInstrument` slice points and `SampleVariable` accidentally moved into packed array | Low | High | Slice points stay as a separate field; `SampleVariable` stays as a `Variable`. Migration script enumerates which `Variable` members move; the rest are annotated as "non-migrated". |
| `Variable` vtable pointer cost for instruments that keep some legacy Variables | Low | Low | Each remaining `Variable` is 32 B. With 1-2 per non-migrated instrument, the cost is trivial (~100 B per instrument). |
| Build time growth from large spec tables | Low | Low | Static `const` arrays of `const char*` are not heavy. No new code generation beyond existing facades. |
| Review fatigue: too many small migration PRs | Medium | Medium | Stage ordering: high-pool-saving instruments (Synth, Sample) go first, so reviewers see the wins early. |

---

## 12. Open questions

1. **Old `Variables()` interface** — keep until stage 8 (current plan)
   or remove earlier? Keeping until stage 8 is safer but keeps the
   dual-path complexity longer.
2. **`ParamRef` vs new `UIField` derived class** — `ParamRef` is
   smaller change (additive constructor overload on existing
   subclasses). New derived class is more invasive but cleaner
   separation. **Plan: `ParamRef`.**
3. **Name at idx 0** — current plan. Alternative is to keep the
   instrument name outside the param array as a separate field.
4. **Sample field** — keep as legacy `Variable` for now (current
   plan), or invest in the richer format API and migrate it?
5. **SID/OPAL migration order** — migrate (preserve), drop outright,
   or keep as `Variable`-only legacy? The pool savings from migration
   are small (1-2 KB) compared to the engineering cost.

---

## 13. Timeline (estimate)

| Stages | Description | Duration | Cumulative |
|---|---|---|---|
| 0–0.8 | New API + facades + UI + persistence switch | 1.5 weeks | 1.5 weeks |
| 1 | Synth migration + first RAM numbers | 1 week | 2.5 weeks |
| 2 | Sample migration | 1 week | 3.5 weeks |
| 3 | MIDI migration | 0.5 week | 4 weeks |
| 4 | SID migration | 1 week | 5 weeks |
| 5 | OPAL migration | 1 week | 6 weeks |
| 6 | Final regression on Advance and Pico | 0.5 week | 6.5 weeks |
| 7 | Stage 8: legacy path deletion | 1 week | 7.5 weeks |
| 8 | Documentation, final pass | 0.5 week | 8 weeks |

**Total: ≈ 8 weeks for the full migration, deliverable as 8-9
mergeable PRs.**

If the team only wants Synth and Sample (the high-value cases), the
critical-path is stages 0-2 plus verification: **≈ 3.5 weeks** to the
first major checkpoint where ≈ 17 KB is reclaimed.
