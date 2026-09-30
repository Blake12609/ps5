# Third-party dependencies, fetched at configure time and linked statically.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Some dependencies still declare very old minimum CMake versions (rejected by CMake 4).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# JSON (config files, GitHub release API)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# hidapi (raw DualSense HID reports)
FetchContent_Declare(hidapi
  GIT_REPOSITORY https://github.com/libusb/hidapi.git
  GIT_TAG hidapi-0.15.0
  GIT_SHALLOW TRUE)
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_HIDRAW ON CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(hidapi)
if(WIN32)
  set(EDGEPAD_HIDAPI_TARGET hidapi::winapi)
elseif(APPLE)
  set(EDGEPAD_HIDAPI_TARGET hidapi::darwin)
else()
  set(EDGEPAD_HIDAPI_TARGET hidapi::hidraw)
endif()

# ViGEmClient (virtual Xbox 360 / DualShock 4 controller on Windows)
if(WIN32)
  FetchContent_Declare(vigemclient
    GIT_REPOSITORY https://github.com/nefarius/ViGEmClient.git
    GIT_TAG v1.21.222.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(vigemclient)
endif()

if(EDGEPAD_BUILD_GUI)
  find_package(OpenGL REQUIRED)

  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)

  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)

  add_library(edgepad_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(edgepad_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(edgepad_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  target_link_libraries(edgepad_imgui PUBLIC glfw OpenGL::GL)
endif()

if(EDGEPAD_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
endif()
