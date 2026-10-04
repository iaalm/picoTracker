cmake_minimum_required(VERSION 3.16)

#
# Host (desktop) build of the picoTracker firmware -- the "emulator".
#
# This is a native port, not an instruction-level RP2040 emulator: the same
# Application/UIFramework/Services/Foundation/System sources the firmware uses
# are compiled for the host and bound to an SDL adapter instead of the pico
# one. It reproduces tracker behaviour (UI, sequencing, synthesis); it does
# NOT reproduce the device's RAM budget, stack limits or timing.
#
#   cmake -S emulator -B build-sdl -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-sdl -j 4
#
# If SDL3 is installed outside the default prefix, add
#   -DCMAKE_PREFIX_PATH=$HOME/.local/sdl3
#

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")

project(picoTrackerSDL C CXX)

set(PT_SOURCES ${CMAKE_CURRENT_LIST_DIR}/../sources)

if(EMSCRIPTEN)
  # Emscripten ships its own SDL3 port; find_package would pick up a host
  # SDL3 and link the wrong ABI, so bypass it entirely.
  add_library(SDL3::SDL3 INTERFACE IMPORTED)
  target_compile_options(SDL3::SDL3 INTERFACE "-sUSE_SDL=3")
  target_link_options(SDL3::SDL3 INTERFACE "-sUSE_SDL=3")
else()
  find_package(SDL3 QUIET)
  if(NOT SDL3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
  endif()
endif()

# Mirrors the device build's warning set so host builds catch the same
# mistakes.
add_compile_options(
  -Wall
  -Wno-format
  -Wno-unused-variable
  -Wno-array-bounds
  -Wno-unused-parameter
  -Wno-missing-field-initializers
)

add_compile_definitions(PICO_TRACKER_SDL=1)

# The instrument headers assert their on-device struct sizes. Those budgets
# assume the RP2040's 32-bit pointers and char-sized ETL enums, so they can't
# hold on a 64-bit host. The headers already gate them behind HOST_TEST (the
# same switch tests/ uses); the real budgets stay enforced by the device
# build.
add_compile_definitions(HOST_TEST=1)

# Unlike the firmware we do NOT force-include config/no_heap.h: SDL and the
# host C++ runtime allocate. The device build still enforces no-heap.

include_directories(${PT_SOURCES})
include_directories(${PT_SOURCES}/config)
# The core includes "platform.h"; resolve it to the SDL stand-in rather than
# the pico one.
include_directories(${PT_SOURCES}/Adapters/sdl/platform)
# A few core files include the device adapter by path (record.h, utils.h).
# This directory shadows those paths with host equivalents; see the shims'
# comments. Must come before ${PT_SOURCES} to win.
include_directories(BEFORE ${PT_SOURCES}/Adapters/sdl/compat)

add_subdirectory(${PT_SOURCES}/Externals ${CMAKE_BINARY_DIR}/Externals)
# Defines the `platform` target that Application/ links against, so it has to
# come before the core layers.
add_subdirectory(${PT_SOURCES}/Adapters/sdl ${CMAKE_BINARY_DIR}/AdaptersSDL)
add_subdirectory(${PT_SOURCES}/Foundation ${CMAKE_BINARY_DIR}/Foundation)
add_subdirectory(${PT_SOURCES}/System ${CMAKE_BINARY_DIR}/System)
add_subdirectory(${PT_SOURCES}/UIFramework ${CMAKE_BINARY_DIR}/UIFramework)
add_subdirectory(${PT_SOURCES}/Services ${CMAKE_BINARY_DIR}/Services)
add_subdirectory(${PT_SOURCES}/Application ${CMAKE_BINARY_DIR}/Application)

add_executable(picoTrackerSDL
  ${PT_SOURCES}/Adapters/sdl/main/sdlMain.cpp
)

if(EMSCRIPTEN)
  # The SD card root is baked into the .data file at build time and mounted
  # read-write in MEMFS. Writes (saving a project) therefore live only in the
  # tab -- reloading the page discards them.
  set(PT_WEB_SDROOT "${CMAKE_CURRENT_LIST_DIR}/../test_root"
      CACHE PATH "Directory preloaded as the emulator's SD card root")
  if(NOT EXISTS "${PT_WEB_SDROOT}")
    message(FATAL_ERROR
      "PT_WEB_SDROOT does not exist: ${PT_WEB_SDROOT}\n"
      "Point it at an SD card root, e.g. -DPT_WEB_SDROOT=/path/to/test_root")
  endif()

  set_target_properties(picoTrackerSDL PROPERTIES SUFFIX ".html")

  # Emscripten emits four sibling files (.html/.js/.wasm/.data) and CMake drops
  # them next to the object tree by default. Send them to a directory of their
  # own instead: that directory is then exactly the set of files a web server
  # should expose, so it can be served directly -- no copy step, and no risk of
  # publishing sources or .o files alongside them.
  #
  # The basename is `index` so the directory is servable as-is, without an
  # index.html duplicated from the real page.
  set_target_properties(picoTrackerSDL PROPERTIES
    OUTPUT_NAME "index"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

  target_link_options(picoTrackerSDL PRIVATE
    "-sUSE_SDL=3"
    # The sample arena alone is a static 8MB; give the heap room up front so
    # startup doesn't immediately trigger a grow-and-copy.
    "-sINITIAL_MEMORY=134217728"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sSTACK_SIZE=1048576"
    # main() returns immediately under emscripten_set_main_loop; keep the
    # runtime (and the preloaded filesystem) alive for the frame callbacks.
    "-sEXIT_RUNTIME=0"
    "-sASYNCIFY=0"
    # The shell page's touch pad calls into the event manager directly.
    "-sEXPORTED_FUNCTIONS=['_main','_pt_set_touch_mask']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "--preload-file" "${PT_WEB_SDROOT}@/sdcard"
    "--shell-file" "${CMAKE_CURRENT_LIST_DIR}/web/shell.html"
  )
  set_property(TARGET picoTrackerSDL APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/web/shell.html")
endif()

target_link_libraries(picoTrackerSDL PRIVATE
  sdl_adapter
  application
  application_views
  application_views_baseclasses
  uiframework_simplebaseclasses
  uiframework_interfaces
  uiframework_basicdatas
  foundation
  foundation_variables
  foundation_services
  services_audio
  services_midi
  application_audio
  application_mixer
  system_timer
  system_console
)
