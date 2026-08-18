# Host tests and tools

This directory builds host-only binaries using the same `tests/` CMake project.

## Build

From repo root:

```bash
cmake -S tests -B build-host
cmake --build build-host
```

## Unit tests

```bash
./build-host/picoTracker_tests
```

## Synth offline renderer (`synth_render`)

Render [`SynthVoice`](../sources/Application/Instruments/SynthVoice.cpp) to a WAV file on your Mac — no hardware required.

```bash
# List presets
./build-host/synth_render --list

# Single note
./build-host/synth_render --preset fm_bass --note 48 --seconds 3 --play

# Built-in arpeggio sequence
./build-host/synth_render --preset arp_ceg --play

# Custom note sequence (release@t or off@t to gate off)
./build-host/synth_render --preset default \
  --sequence 60@0,64@0.5,67@1.0,release@2.0 --seconds 4 --play

# Legato slide (no envelope retrigger)
./build-host/synth_render --preset default \
  --sequence 60@0,legato:67@0.8,legato:72@1.6,release@2.4 --play
```

Output defaults to `/tmp/synth_<preset>.wav`. Use `-o path.wav` to override.

Single-note presets without `release_at` automatically gate off at ~60% of the clip
so the release tail can finish; WAV length is trimmed once the voice goes silent.

### Presets

Edit [`presets/synth_presets.cpp`](presets/synth_presets.cpp) to add `VoiceParams` and optional `NoteEvent` sequences, then rebuild.

| Preset | Description |
|--------|-------------|
| `default` | Plain sine additive |
| `fm_bass` | FM bass, auto release at 2s |
| `saw_pad` | Detuned saw pad with LFO on cutoff |
| `arp_ceg` | C major arpeggio |
| `legato_slide` | Legato pitch slide |
