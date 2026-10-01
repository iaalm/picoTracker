# picoTracker Emulator (SDL desktop build)

Runs the picoTracker firmware natively on a desktop, using an SDL adapter in
place of the RP2040 one.

## What this is — and isn't

This is a **native port**, not an instruction-level RP2040 emulator. The same
`Application/`, `UIFramework/`, `Services/`, `Foundation/` and `System/`
sources the firmware uses are compiled for the host and bound to
`sources/Adapters/sdl/` instead of `sources/Adapters/picoTracker/`.

It faithfully reproduces:

* the UI, navigation and rendering (same 32×24 character grid, same fonts)
* sequencing, playback timing and the sample/synth engines
* project load/save and the file browser

It does **not** reproduce:

* the device's RAM budget, stack limits (`-Wstack-usage`) or flash wear
* RP2040 timing, dual-core scheduling, or the 8-channel performance ceiling
* MIDI I/O (no devices are registered yet) or audio recording

So it's the right tool for working on UI, sequencing and instrument
behaviour, and the wrong tool for answering "does this still fit on the
hardware?" — keep using the firmware build for that.

## Building

Requires SDL3 and a C++23 compiler.

```sh
cmake -S emulator -B build-sdl -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdl -j 4
```

If SDL3 is installed outside the default prefix, point CMake at it:

```sh
cmake -S emulator -B build-sdl -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$HOME/.local/sdl3
```

On distributions that ship SDL3 but not its headers (AlmaLinux 10, for
example), the `SDL3-devel` package can be unpacked into a user prefix without
root:

```sh
dnf download SDL3-devel
rpm2cpio SDL3-devel-*.rpm | (mkdir -p /tmp/sdl3 && cd /tmp/sdl3 && cpio -idm)
mkdir -p ~/.local/sdl3/lib64
cp -r /tmp/sdl3/usr/include ~/.local/sdl3/
cp -r /tmp/sdl3/usr/lib64/pkgconfig ~/.local/sdl3/lib64/
ln -sf /usr/lib64/libSDL3.so.0 ~/.local/sdl3/lib64/libSDL3.so
sed -i "s|^prefix=.*|prefix=$HOME/.local/sdl3|" ~/.local/sdl3/lib64/pkgconfig/sdl3.pc
```

Then build with `PKG_CONFIG_PATH=$HOME/.local/sdl3/lib64/pkgconfig`.

## Running

```sh
./build-sdl/picoTrackerSDL --sdroot test_root
```

`--sdroot` is the directory presented to the firmware as the SD card. The
emulator cannot read or write outside it.

| Option | Meaning |
| --- | --- |
| `--sdroot <dir>` | directory used as the SD card root (default `test_root`) |
| `--scale <n>` | window scale factor (default 2, giving 640×480) |
| `--exit-after <ms>` | quit automatically after n milliseconds |
| `--screenshot <path>` | write a BMP just before exiting |
| `--keys <list>` | replay keys at startup, comma separated |

### Keys

| Device button | Keyboard |
| --- | --- |
| D-pad | arrow keys |
| ENTER | `A` or `Z` |
| EDIT | `S` or `X` |
| ALT | shift |
| NAV | ctrl |
| PLAY | space |
| — | `Esc` quits |

### Headless use

With SDL's dummy backends the emulator runs with no display or sound card,
which makes it usable from CI:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build-sdl/picoTrackerSDL --sdroot test_root \
    --keys play --exit-after 5000 --screenshot /tmp/shot.bmp
```

Audio can be captured to a file for inspection:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=disk \
  ./build-sdl/picoTrackerSDL --sdroot test_root --keys play --exit-after 8000
# writes sdlaudio.raw (S16LE stereo 44.1kHz) in the working directory
```

## How it maps onto the firmware

| Subsystem | Device | Emulator |
| --- | --- | --- |
| Display | ILI9341 over SPI (`chargfx`) | SDL texture, same character grid |
| Input | GPIO scan + debounce | SDL key events (no debounce needed) |
| Audio | PIO + DMA I2S, mixer on core1 | SDL audio stream, mixer on the audio thread |
| Storage | SD card via SdFat/SDIO | POSIX I/O confined to `--sdroot` |
| Samples | programmed into flash, played from XIP | 8 MB RAM arena |
| Timers | hardware alarms | `SDL_AddTimer` |
| Mutex | `pico/mutex.h` | `std::recursive_mutex` |
| MIDI | UART + USB devices | none registered (messages dropped) |

The sample arena is deliberately capped at the device's 8 MB budget so that
"this sample doesn't fit" behaves the same way it does on hardware.

## Known gaps

* **MIDI** — `sdlMidiService` registers no devices, so MIDI output goes
  nowhere. The sequencer's MIDI timing path still runs.
* **Recording** — stubbed out, matching the pico build (which is also a no-op,
  as the original hardware has no line-in).
* **Struct-size asserts** — the instrument headers' on-device size budgets are
  disabled here via `HOST_TEST`, because they assume 32-bit pointers. The
  device build still enforces them.
* **`Adapters/sdl/compat/`** — three core files (`AppWindow.cpp`,
  `Views/RecordView.cpp`, `Instruments/MacroInstrument.cpp`) include the pico
  adapter by path. That directory shadows those includes with host
  equivalents; if the layering leak is fixed upstream it can be deleted.
