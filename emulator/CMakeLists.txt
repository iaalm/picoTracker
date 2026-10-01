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

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
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
