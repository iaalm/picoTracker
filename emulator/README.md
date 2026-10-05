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

## Building for the web (WebAssembly)

The same sources build to WebAssembly with Emscripten, which ships its own
SDL3 port — no host SDL3 is involved.

```sh
git clone https://github.com/emscripten-core/emsdk
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh

cd /path/to/picoTracker
emcmake cmake -S emulator -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j 4
```

The output is `build-web/dist/index.{html,js,wasm,data}` — a directory holding
those four files and nothing else, so it can be served or published as-is. It
must be served over HTTP (opening the `.html` from `file://` fails the `.data`
fetch):

```sh
cd build-web/dist && python3 -m http.server 8000
# then open http://localhost:8000/
```

### The SD card is baked in

There is no `--sdroot` in a browser. The directory named by `PT_WEB_SDROOT`
(default: `test_root/`) is packed into `dist/index.data` at link time and
mounted at `/sdcard` in MEMFS:

```sh
emcmake cmake -S emulator -B build-web -DPT_WEB_SDROOT=/path/to/sdcard
```

Two consequences worth knowing:

* **Writes don't persist.** Saving a project modifies MEMFS, which is
  discarded when the tab reloads. Nothing is written back to the host.
* **CMake does not notice content changes** inside `PT_WEB_SDROOT` — only
  the link step packs it. After editing files in that directory, force a
  relink:

  ```sh
  rm -f build-web/dist/index.data && cmake --build build-web
  ```

Which project opens is decided by the `.current` file in that directory, as
on the device.

### Browser specifics

* A click is required before audio starts; the shell page shows a
  "click to start" overlay because autoplay policy keeps the AudioContext
  suspended until a user gesture.
* The browser owns the event loop, so `MainLoop()` hands one frame at a time
  back via `emscripten_set_main_loop` instead of blocking — see
  `sdlEventManager::RunOneFrame`.
* `--keys`, `--screenshot` and `--exit-after` are desktop-only (they come
  from argv).

### Touch (phones and tablets)

On coarse-pointer devices the shell shows an on-screen keypad. It supports
simultaneous presses, which the tracker needs: ALT+arrow and NAV+arrow are
how you reach half the UI.

The pad doesn't synthesise key events. It tracks which button each active
pointer is over and pushes the whole set of held buttons into the firmware
as a bitmask (`pt_set_touch_mask` → `sdlEventManager::SetTouchMask`), which
is the same shape as the device keypad. Two consequences:

* multi-touch works by construction — two fingers are two map entries that
  OR together, and no press/release pair can desync the state;
* keyboard and pad are kept in separate masks and OR'd per frame, so a
  key-up can't clear a button the pad is still holding.

Buttons are resolved by hit-testing the pointer position on every move
rather than trusting the element that received `pointerdown`, so sliding
between buttons behaves sensibly and a finger dragged off the pad releases
instead of sticking.

One constraint worth knowing if you touch the layout: the canvas's CSS box
must stay an exact multiple of the 320×240 grid. SDL's emscripten backend
derives the drawing buffer from the element's on-screen rect, so a
percentage width or a CSS transform resizes the buffer itself (420×315 and
404×303 respectively were observed) and the UI renders corrupted. `fitStage()`
picks the largest integer multiple that fits.

### Headless verification

Two scripts drive the web build through a real headless Chrome, which is the
only way to check it: SDL3's web backend needs `window`, so plain Node can't
run the module.

```sh
npx @puppeteer/browsers install chrome-headless-shell@stable --path .browser
npm install --no-save puppeteer-core

cd build-web/dist && python3 -m http.server 8731 &
node emulator/web/smoke.cjs      # renders? does the frame change on input?
node emulator/web/audiocheck.cjs # do non-silent samples reach the output?
node emulator/web/touchcheck.cjs # do two/three contacts produce one mask?
node emulator/web/padui.cjs      # does a pad press actually move the UI?
node emulator/web/combocheck.cjs # does NAV+UP switch screens?
node emulator/web/entercheck.cjs # ENTER+UP on a field owning no Variable
node emulator/web/settingscheck.cjs # walk every settings screen and field
```

`entercheck.cjs` and `settingscheck.cjs` need a bundled project that has
instruments; against the default empty one they stop at the instrument
screen rather than reporting a pass they did not earn.

`settingscheck.cjs` drives the UI against the character grid the firmware
rendered, via the `pt_get_screen_text` / `pt_get_cursor_row` hooks the web
build exports. Every navigation step asserts where it landed, because a
script that only presses buttons and checks for a crash will happily "pass"
while stuck on the wrong screen — which happened repeatedly here before the
hooks existed. Its route map (NAV+DOWN reaches the table from the instrument
screen, the mixer keeps the "Song" title, device and project unwind with
NAV+DOWN while the chain screens use NAV+LEFT) was read off each view's
`ProcessButtonMask`, not guessed.

Run it against a `PT_WEB_DEBUG` build to get the assertions and SAFE_HEAP
checks: it has found crashes there that the Release build swallows silently.

### Debugging a wasm trap

A Release build reports only `Out of bounds call_indirect` with no hint of
where. Configure with `-DPT_WEB_DEBUG=ON` for a build with assertions,
SAFE_HEAP, stack checks and UBSan — it names the C++ frames:

```sh
emcmake cmake -S emulator -B build-web-dbg \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPT_WEB_DEBUG=ON
cmake --build build-web-dbg -j 4
```

The wasm goes from ~1.5 MB to ~13 MB and runs much slower, so this is for
diagnosis only. Emscripten's default shell has no error UI; on a phone,
inject an overlay that prints `window.onerror` to the page, since there is no
console to read.

Worth knowing: an indirect call that merely misbehaves on the device **traps**
here. wasm checks the function-table index and the signature on every
`call_indirect`, so a C-style downcast to the wrong field type is fatal rather
than merely lucky. That is a feature — it surfaced a latent bug the hardware
had been tolerating.

`smoke.cjs` screenshots the canvas element rather than reading the WebGL
buffer back: the context has `preserveDrawingBuffer: false`, so a readback
after present returns empty even while the UI draws correctly.
`audiocheck.cjs` patches `AudioNode.connect` before the module loads — a tap
installed afterwards misses SDL's connection and reports false silence.
The touch scripts drive CDP's `Input.dispatchTouchEvent` directly, because
puppeteer's own touchscreen helper models a single contact and so cannot
express a combo.

`combocheck.cjs` uses NAV+UP rather than NAV+RIGHT deliberately: NAV+RIGHT
opens the chain under the cursor and correctly does nothing when that cell
is empty, which on the bundled project looks identical to a broken combo.

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
* **Web: no persistence** — MEMFS is discarded on reload. Wiring up IDBFS
  would fix this but hasn't been done.
* **Web: single-threaded** — built without pthreads, so the mixer runs on the
  main thread's audio callback. Fine for playback; it diverges further from
  the device's core0/core1 split than the desktop build does.
