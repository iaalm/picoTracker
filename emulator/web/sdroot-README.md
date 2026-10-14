# Bundled SD card root for the web build

`sdroot/` next to this file is the SD card the WebAssembly build boots
against. CMake packs it into `dist/index.data` at link time and mounts it at
`/sdcard` in MEMFS, so it is the only storage the browser build has.

This README sits outside `sdroot/` deliberately — everything inside that
directory is packed verbatim and would otherwise show up in the tracker's own
file browser.

It is deliberately the *minimum* that boots, because every byte there is
downloaded by every visitor. The local `test_root/` is 27 MB of sample-heavy
projects and is gitignored; pointing the web build at it produced a 27 MB
`index.data` against 1.7 MB for this one.

## Why `projects/.untitled/lgptsav.dat` has to be here

The firmware creates the root directories it needs on its own
(`Application.cpp`, `ensurePTDirsExist`) — `projects/`, `samples/`,
`instruments/`, `renders/`, `themes/`, `recordings/` — and writes a default
`.config.xml` when none exists. So none of those need committing.

What it does *not* do is create the default project's data file. Booting
against a genuinely empty root stops at `Invalid Project: .untitled` before
reaching the song screen. The 15-byte `<PICOTRACKER/>` file is what gets it
past that, and it's the same content `CreateProject()` writes.

`.current` is not committed either: absent, the app falls back to `.untitled`,
which is what we want anyway.

## Shipping a demo project instead

To publish with a song loaded rather than an empty one, point the build at a
different directory — nothing here is hardcoded:

```sh
emcmake cmake -S emulator -B build-web -DPT_WEB_SDROOT=/path/to/sdcard
```

Add a `.current` naming the project in that root. Mind the download size, and
note that CMake does not notice content changes inside `PT_WEB_SDROOT` — only
the link step packs it, so force a relink after editing:

```sh
rm -f build-web/dist/index.data && cmake --build build-web
```
