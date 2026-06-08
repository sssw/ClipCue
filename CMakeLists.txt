cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
project(ClipCue VERSION 0.2.0 LANGUAGES CXX)

option(CLIPCUE_BUILD_MODERN_SHELL "Build experimental Windows 11 IExplorerCommand shell extension" OFF)
option(CLIPCUE_BUILD_TESTS "Build smoke tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "ClipCue is a Windows-only project. Build on windows-latest or a Windows machine.")
endif()

add_compile_definitions(
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0601
  CLIPCUE_VERSION=L"${PROJECT_VERSION}"
)

if(MSVC)
  # Keep GitHub Actions artifacts easy to test on clean machines by avoiding a
  # separate VC++ redistributable dependency in the engineering preview.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /EHsc /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CLIPCUE_COMMON_HEADERS
  include/clipcue/Version.h
  include/clipcue/WinUtils.h
  include/clipcue/PathUtils.h
  include/clipcue/CryptoStore.h
  include/clipcue/History.h
  include/clipcue/Job.h
  include/clipcue/FileOps.h
)

set(CLIPCUE_COMMON_SOURCES
  src/common/WinUtils.cpp
  src/common/PathUtils.cpp
  src/common/CryptoStore.cpp
  src/common/History.cpp
  src/common/Job.cpp
  src/common/FileOps.cpp
)

add_library(clipcue_common STATIC ${CLIPCUE_COMMON_HEADERS} ${CLIPCUE_COMMON_SOURCES})
target_link_libraries(clipcue_common PUBLIC shell32 shlwapi ole32 uuid advapi32 crypt32 user32)

target_compile_definitions(clipcue_common PUBLIC CLIPCUE_HAS_DPAPI_STORE=1)

add_executable(ClipCue.Agent src/agent/Agent.cpp)
target_link_libraries(ClipCue.Agent PRIVATE clipcue_common comdlg32)

add_executable(ClipCue.Worker src/worker/Worker.cpp)
target_link_libraries(ClipCue.Worker PRIVATE clipcue_common)

add_executable(ClipCue.Elevated src/elevated/Elevated.cpp src/elevated/Elevated.rc)
target_link_libraries(ClipCue.Elevated PRIVATE clipcue_common)
target_include_directories(ClipCue.Elevated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/elevated)
if(MSVC)
  target_link_options(ClipCue.Elevated PRIVATE /MANIFEST:NO)
endif()

add_executable(ClipCue.Settings src/settings/Settings.cpp)
target_link_libraries(ClipCue.Settings PRIVATE clipcue_common)

add_executable(ClipCue.Monitor WIN32 src/monitor/Monitor.cpp)
target_link_libraries(ClipCue.Monitor PRIVATE clipcue_common shell32 gdi32)

add_executable(ClipCue.UI WIN32 src/ui/ClipCueUI.cpp)
target_link_libraries(ClipCue.UI PRIVATE clipcue_common comctl32 comdlg32 ole32 shell32 shlwapi gdi32)

add_executable(ClipCue.Installer WIN32 src/installer/Installer.cpp)
target_link_libraries(ClipCue.Installer PRIVATE clipcue_common comctl32 ole32 shell32 shlwapi advapi32 gdi32)

add_library(ClipCue.ShellClassic SHARED
  src/shell_classic/ShellClassic.cpp
  src/shell_classic/ShellClassic.def
)
target_link_libraries(ClipCue.ShellClassic PRIVATE shell32 shlwapi ole32 uuid advapi32 user32)
set_target_properties(ClipCue.ShellClassic PROPERTIES OUTPUT_NAME "ClipCue.ShellClassic")

if(CLIPCUE_BUILD_MODERN_SHELL)
  add_library(ClipCue.ShellModern SHARED
    src/shell_modern/ShellModern.cpp
    src/shell_modern/ShellModern.def
  )
  target_link_libraries(ClipCue.ShellModern PRIVATE shell32 shlwapi ole32 uuid advapi32 user32)
endif()

if(CLIPCUE_BUILD_TESTS)
  enable_testing()
  add_executable(ClipCue.Tests tests/ClipCue.Tests.cpp)
  target_link_libraries(ClipCue.Tests PRIVATE clipcue_common)
  add_test(NAME ClipCueSmoke COMMAND ClipCue.Tests)
endif()

install(TARGETS
  ClipCue.Agent
  ClipCue.Worker
  ClipCue.Elevated
  ClipCue.Settings
  ClipCue.Monitor
  ClipCue.UI
  ClipCue.Installer
  ClipCue.ShellClassic
  RUNTIME DESTINATION .
  LIBRARY DESTINATION .
  ARCHIVE DESTINATION lib
)
install(FILES LICENSE NOTICE README.md DESTINATION .)
install(DIRECTORY scripts docs DESTINATION .)
