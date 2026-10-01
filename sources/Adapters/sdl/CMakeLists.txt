# Small, dependency-free target matching the device adapter's `platform`
# library. The core links against this for platform_mutex(), so it must not
# depend on the application layer or a link cycle results.
add_library(platform
  platform/platform.cpp
  mutex/sdlMutex.cpp
)

target_link_libraries(platform PUBLIC SDL3::SDL3)

target_include_directories(platform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

# Application/Views links `platform_display` and `platform_audio` by name
# (they're the device's LCD and recording libraries). Provide host targets
# under the same names so the core CMake files work unchanged.
add_library(platform_display INTERFACE)
target_include_directories(platform_display INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/display
)

add_library(platform_audio
  audio/record.cpp
)
target_include_directories(platform_audio PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/audio
)

add_library(sdl_adapter
  system/sdlSystem.cpp
  system/sdlSamplePool.cpp
  gui/sdlGUIWindowImp.cpp
  gui/sdlGUIFactory.cpp
  gui/sdlEventManager.cpp
  display/sdlchargfx.cpp
  timer/sdlTimer.cpp
  audio/sdlAudio.cpp
  audio/sdlAudioDriver.cpp
  filesystem/sdlFileSystem.cpp
)

target_link_libraries(sdl_adapter PUBLIC
  SDL3::SDL3
  platform
  application
  uiframework_interfaces
  uiframework_simplebaseclasses
  system_console
  system_process
  remote_ui
  etl
)

target_include_directories(sdl_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
