cmake_minimum_required(VERSION 3.13)

project(picoTrackerHostTests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(HOST_TEST_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/helpers
  ${CMAKE_CURRENT_LIST_DIR}/presets
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
)

add_executable(picoTracker_tests
  test_main.cpp
  foundation_types_tests.cpp
  wavheader_tests.cpp
  synth_voice_tests.cpp
  ../sources/Application/Instruments/WavHeader.cpp
  ../sources/Application/Instruments/SynthVoice.cpp
)

target_include_directories(picoTracker_tests PRIVATE ${HOST_TEST_INCLUDES})

target_compile_definitions(picoTracker_tests PRIVATE
  HOST_TEST
)

add_executable(synth_render
  tools/synth_render.cpp
  presets/synth_presets.cpp
  ../sources/Application/Instruments/SynthVoice.cpp
  ../sources/Application/Instruments/WavHeader.cpp
)

target_include_directories(synth_render PRIVATE ${HOST_TEST_INCLUDES})

target_compile_definitions(synth_render PRIVATE
  HOST_TEST
)

add_executable(picoTracker_persist
  test_main.cpp
  persist_tests.cpp
  stubs/InstrumentHostStubs.cpp
  ../sources/Application/Instruments/I_Instrument.cpp
  ../sources/Application/Instruments/InstrumentNameVariable.cpp
  ../sources/Application/Instruments/SampleInstrument.cpp
  ../sources/Application/Instruments/SRPUpdaters.cpp
  ../sources/Application/Instruments/SynthInstrument.cpp
  ../sources/Application/Instruments/SynthVoice.cpp
  ../sources/Application/Instruments/OpalInstrument.cpp
  ../sources/Application/Instruments/SIDInstrument.cpp
  ../sources/Application/Instruments/MidiInstrument.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Persistency/PersistencyService.cpp
  ../sources/Application/Persistency/Persistent.cpp
  ../sources/Foundation/Services/Service.cpp
  ../sources/Foundation/Services/SubService.cpp
  ../sources/Foundation/Services/ServiceRegistry.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/System/Console/Trace.cpp
  ../sources/System/Console/n_assert.cpp
  ../sources/Externals/yxml/yxml.c
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Application/Utils/char.cpp
)

target_include_directories(picoTracker_persist PRIVATE
  ${HOST_TEST_INCLUDES}
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Utils
  ${CMAKE_CURRENT_LIST_DIR}/../sources/System/Profiler
)

target_link_libraries(picoTracker_persist PRIVATE
  m
)

# The instrument .cpp files reference DSP / service symbols that have no
# host implementation (Opal OPL3, cRSID SID, MidiService, SyncMaster, ...).
# We don't call any of these code paths from the persistence tests — the
# tests only read the static NAMES/SPECS/FORMATS tables and call
# GetParamName/GetParamCount/FindParamByName. Tolerate the unresolved
# references rather than stubbing the entire DSP/service layer; the
# InstrumentHostStubs.cpp file covers the small set of symbols that ARE
# referenced from the (host-compiled) instrument .cpp files we link.
target_link_options(picoTracker_persist PRIVATE
  -Wl,--unresolved-symbols=ignore-all
)

target_compile_definitions(picoTracker_persist PRIVATE
  HOST_TEST
  NDEBUG
  TEST_FIXTURE_PATH="${CMAKE_CURRENT_LIST_DIR}/../test_root/projects/lgpt_CTX2"
  TEST_PROJECTS_ROOT="${CMAKE_CURRENT_LIST_DIR}/../test_root/projects"
)
