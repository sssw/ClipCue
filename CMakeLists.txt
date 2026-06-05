cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
project(PathCue VERSION 0.2.0 LANGUAGES CXX)

option(PATHCUE_BUILD_MODERN_SHELL "Build experimental Windows 11 IExplorerCommand shell extension" OFF)
option(PATHCUE_BUILD_TESTS "Build smoke tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "PathCue is a Windows-only project. Build on windows-latest or a Windows machine.")
endif()

add_compile_definitions(
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0601
  PATHCUE_VERSION=L"${PROJECT_VERSION}"
)

if(MSVC)
  # Keep GitHub Actions artifacts easy to test on clean machines by avoiding a
  # separate VC++ redistributable dependency in the engineering preview.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /EHsc /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(PATHCUE_COMMON_HEADERS
  include/pathcue/Version.h
  include/pathcue/WinUtils.h
  include/pathcue/PathUtils.h
  include/pathcue/CryptoStore.h
  include/pathcue/History.h
  include/pathcue/Job.h
  include/pathcue/FileOps.h
)

set(PATHCUE_COMMON_SOURCES
  src/common/WinUtils.cpp
  src/common/PathUtils.cpp
  src/common/CryptoStore.cpp
  src/common/History.cpp
  src/common/Job.cpp
  src/common/FileOps.cpp
)

add_library(pathcue_common STATIC ${PATHCUE_COMMON_HEADERS} ${PATHCUE_COMMON_SOURCES})
target_link_libraries(pathcue_common PUBLIC shell32 shlwapi ole32 uuid advapi32 crypt32 user32)

target_compile_definitions(pathcue_common PUBLIC PATHCUE_HAS_DPAPI_STORE=1)

add_executable(PathCue.Agent src/agent/Agent.cpp)
target_link_libraries(PathCue.Agent PRIVATE pathcue_common comdlg32)

add_executable(PathCue.Worker src/worker/Worker.cpp)
target_link_libraries(PathCue.Worker PRIVATE pathcue_common)

add_executable(PathCue.Elevated src/elevated/Elevated.cpp src/elevated/Elevated.rc)
target_link_libraries(PathCue.Elevated PRIVATE pathcue_common)
target_include_directories(PathCue.Elevated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/elevated)
if(MSVC)
  target_link_options(PathCue.Elevated PRIVATE /MANIFEST:NO)
endif()

add_executable(PathCue.Settings src/settings/Settings.cpp)
target_link_libraries(PathCue.Settings PRIVATE pathcue_common)

add_executable(PathCue.Monitor WIN32 src/monitor/Monitor.cpp)
target_link_libraries(PathCue.Monitor PRIVATE pathcue_common shell32 gdi32)

add_executable(PathCue.UI WIN32 src/ui/PathCueUI.cpp)
target_link_libraries(PathCue.UI PRIVATE pathcue_common comctl32 comdlg32 ole32 shell32 shlwapi)

add_executable(PathCue.Installer WIN32 src/installer/Installer.cpp)
target_link_libraries(PathCue.Installer PRIVATE pathcue_common comctl32 ole32 shell32 shlwapi advapi32)

add_library(PathCue.ShellClassic SHARED
  src/shell_classic/ShellClassic.cpp
  src/shell_classic/ShellClassic.def
)
target_link_libraries(PathCue.ShellClassic PRIVATE shell32 shlwapi ole32 uuid advapi32 user32)
set_target_properties(PathCue.ShellClassic PROPERTIES OUTPUT_NAME "PathCue.ShellClassic")

if(PATHCUE_BUILD_MODERN_SHELL)
  add_library(PathCue.ShellModern SHARED
    src/shell_modern/ShellModern.cpp
    src/shell_modern/ShellModern.def
  )
  target_link_libraries(PathCue.ShellModern PRIVATE shell32 shlwapi ole32 uuid advapi32 user32)
endif()

if(PATHCUE_BUILD_TESTS)
  enable_testing()
  add_executable(PathCue.Tests tests/PathCue.Tests.cpp)
  target_link_libraries(PathCue.Tests PRIVATE pathcue_common)
  add_test(NAME PathCueSmoke COMMAND PathCue.Tests)
endif()

install(TARGETS
  PathCue.Agent
  PathCue.Worker
  PathCue.Elevated
  PathCue.Settings
  PathCue.Monitor
  PathCue.UI
  PathCue.Installer
  PathCue.ShellClassic
  RUNTIME DESTINATION .
  LIBRARY DESTINATION .
  ARCHIVE DESTINATION lib
)
install(FILES LICENSE NOTICE README.md DESTINATION .)
install(DIRECTORY scripts docs DESTINATION .)
