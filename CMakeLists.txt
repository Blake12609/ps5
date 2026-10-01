cmake_minimum_required(VERSION 3.21)

# Force static MSVC runtime in every sub-project so the exe is fully portable.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(EdgePad LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EDGEPAD_BUILD_GUI "Build the desktop GUI (Dear ImGui + GLFW)" ON)
option(EDGEPAD_BUILD_TESTS "Build the unit tests" ON)
option(EDGEPAD_RELEASE_BUILD "Official release build (enables self-update)" OFF)
set(EDGEPAD_BUILD_NUMBER "0" CACHE STRING "Patch number appended to VERSION (CI run number)")
set(EDGEPAD_REPOSITORY "Blake12609/ps5" CACHE STRING "GitHub owner/repo that publishes releases")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" EDGEPAD_BASE_VERSION)
string(STRIP "${EDGEPAD_BASE_VERSION}" EDGEPAD_BASE_VERSION)
set(EDGEPAD_VERSION "${EDGEPAD_BASE_VERSION}.${EDGEPAD_BUILD_NUMBER}")
message(STATUS "EdgePad version ${EDGEPAD_VERSION} (release build: ${EDGEPAD_RELEASE_BUILD})")

if(EDGEPAD_RELEASE_BUILD)
  set(EDGEPAD_RELEASE_BUILD_VALUE 1)
else()
  set(EDGEPAD_RELEASE_BUILD_VALUE 0)
endif()
configure_file(src/core/build_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/build_info.hpp" @ONLY)

include(cmake/Dependencies.cmake)

if(MSVC)
  set(EDGEPAD_WARNINGS /W4 /permissive- /utf-8 /wd4244 /wd4267)
else()
  set(EDGEPAD_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Core: platform independent logic (processing, reports, config, updates)
# ---------------------------------------------------------------------------
add_library(edgepad_core STATIC
  src/core/types.cpp
  src/core/keys.cpp
  src/core/processing.cpp
  src/core/settings.cpp
  src/core/config_json.cpp
  src/core/pipeline.cpp
  src/core/dualsense.cpp
  src/core/device_path.cpp
  src/core/virtual_reports.cpp
  src/core/virtual_dualsense.cpp
  src/core/usbip_session.cpp
  src/core/version.cpp
  src/core/sha256.cpp
  src/core/release_info.cpp
)
target_include_directories(edgepad_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(edgepad_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(edgepad_core PRIVATE ${EDGEPAD_WARNINGS})

# ---------------------------------------------------------------------------
# Platform: HID access, virtual controller, HTTP, paths, self-update
# ---------------------------------------------------------------------------
set(EDGEPAD_PLATFORM_SOURCES
  src/platform/hid_device.cpp
  src/platform/paths.cpp
  src/platform/self_update.cpp
  src/platform/dualsense_bridge.cpp
  src/platform/usbip_server.cpp
)
if(WIN32)
  list(APPEND EDGEPAD_PLATFORM_SOURCES
    src/platform/virtual_pad_vigem.cpp
    src/platform/hid_exclusive_win.cpp
    src/platform/controller_hide_win.cpp
    src/platform/virtual_dualsense_win.cpp
    src/platform/keyboard_win.cpp
    src/platform/http_winhttp.cpp)
else()
  list(APPEND EDGEPAD_PLATFORM_SOURCES
    src/platform/virtual_pad_uinput.cpp
    src/platform/controller_hide_linux.cpp
    src/platform/virtual_dualsense_linux.cpp
    src/platform/keyboard_uinput.cpp
    src/platform/http_curl.cpp)
endif()

add_library(edgepad_platform STATIC ${EDGEPAD_PLATFORM_SOURCES})
target_link_libraries(edgepad_platform PUBLIC edgepad_core ${EDGEPAD_HIDAPI_TARGET})
target_compile_options(edgepad_platform PRIVATE ${EDGEPAD_WARNINGS})
if(WIN32)
  if(MSVC)
    # ViGEmClient's headers contain non UTF-8 characters in comments.
    set_source_files_properties(src/platform/virtual_pad_vigem.cpp PROPERTIES COMPILE_OPTIONS /wd4828)
  endif()
  target_compile_definitions(edgepad_platform PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(edgepad_platform PUBLIC ViGEmClient setupapi hid winhttp ws2_32 shell32 ole32 user32)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(EDGEPAD_APP_SOURCES
  src/app/main.cpp
  src/app/engine.cpp
  src/app/config_store.cpp
  src/app/updater.cpp
)
if(EDGEPAD_BUILD_GUI)
  list(APPEND EDGEPAD_APP_SOURCES src/app/gui.cpp)
endif()

add_executable(EdgePad ${EDGEPAD_APP_SOURCES})
target_link_libraries(EdgePad PRIVATE edgepad_platform)
target_compile_options(EdgePad PRIVATE ${EDGEPAD_WARNINGS})
if(EDGEPAD_BUILD_GUI)
  target_compile_definitions(EdgePad PRIVATE EDGEPAD_HAS_GUI=1)
  target_link_libraries(EdgePad PRIVATE edgepad_imgui)
endif()

find_package(Threads REQUIRED)
target_link_libraries(EdgePad PRIVATE Threads::Threads)

if(WIN32)
  set_target_properties(EdgePad PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(EdgePad PRIVATE /ENTRY:mainCRTStartup)
  else()
    target_link_options(EdgePad PRIVATE -static)
  endif()
elseif(UNIX AND NOT APPLE)
  # Keep the Linux binary portable across distributions.
  target_link_options(EdgePad PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(EDGEPAD_BUILD_TESTS)
  enable_testing()
  add_executable(edgepad_tests
    tests/test_main.cpp
    tests/test_processing.cpp
    tests/test_dualsense.cpp
    tests/test_pipeline.cpp
    tests/test_config.cpp
    tests/test_update.cpp
    tests/test_features.cpp
    tests/test_one_to_one.cpp
    tests/test_virtual_reports.cpp
    tests/test_virtual_dualsense.cpp
    tests/test_usbip_server.cpp
  )
  target_link_libraries(edgepad_tests PRIVATE edgepad_platform doctest::doctest Threads::Threads)
  # Include real std headers instead of doctest's forward declarations (newer MSVC rejects them).
  target_compile_definitions(edgepad_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
  target_compile_options(edgepad_tests PRIVATE ${EDGEPAD_WARNINGS})
  add_test(NAME edgepad_tests COMMAND edgepad_tests)
endif()
