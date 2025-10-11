# Dependencies.cmake
# Manages all external dependencies using CPM (CMake Package Manager)

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache directory")
set(CPM_USE_LOCAL_PACKAGES ON CACHE BOOL "Use local packages if available")

set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
    TLS_VERIFY ON)
endif()
include(${CPM_DOWNLOAD_LOCATION})

# SDL3 - Graphics library
if(WIN32 AND MSVC)
  CPMAddPackage(
    NAME SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-devel-3.2.24-VC.zip
    DOWNLOAD_ONLY YES
  )
  
  if(SDL3_ADDED)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(SDL3_ARCH "x64")
    else()
      set(SDL3_ARCH "x86")
    endif()

    set(SDL3_LIB_DIR "${SDL3_SOURCE_DIR}/lib/${SDL3_ARCH}")
    set(SDL3_STATIC_LIB "${SDL3_LIB_DIR}/SDL3-static.lib")
    set(SDL3_IMPORT_LIB "${SDL3_LIB_DIR}/SDL3.lib")
    set(SDL3_DLL "${SDL3_LIB_DIR}/SDL3.dll")

    if(EXISTS "${SDL3_STATIC_LIB}")
      add_library(SDL3::SDL3 STATIC IMPORTED)
      set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${SDL3_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL3_SOURCE_DIR}/include"
        INTERFACE_LINK_LIBRARIES "winmm;version;setupapi;advapi32;shell32;ole32;oleaut32;user32;gdi32;imm32"
      )
    else()
      add_library(SDL3::SDL3 SHARED IMPORTED)
      set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_IMPLIB "${SDL3_IMPORT_LIB}"
        IMPORTED_LOCATION "${SDL3_DLL}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL3_SOURCE_DIR}/include"
        INTERFACE_LINK_LIBRARIES "winmm;version;setupapi;advapi32;shell32;ole32;oleaut32;user32;gdi32;imm32"
      )
    endif()

    add_library(SDL3::SDL3main STATIC IMPORTED)
    set_target_properties(SDL3::SDL3main PROPERTIES
      IMPORTED_LOCATION "${SDL3_LIB_DIR}/SDL3main.lib"
      INTERFACE_INCLUDE_DIRECTORIES "${SDL3_SOURCE_DIR}/include"
    )

    message(STATUS "Using prebuilt SDL3 for Windows MSVC from: ${SDL3_SOURCE_DIR}")
  endif()
else()
  CPMAddPackage(
    NAME SDL3
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG release-3.2.0
    OPTIONS
      "SDL_TEST OFF"
      "SDL_DISABLE_UNINSTALL ON"
      "SDL_SHARED OFF"
      "SDL_STATIC ON"
      "SDL_AUDIO OFF"
  )
endif()

# plog - Logging library
CPMAddPackage(
  NAME plog
  GITHUB_REPOSITORY SergiusTheBest/plog
  GIT_TAG 1.1.10
  DOWNLOAD_ONLY YES
)

# ImGui - Immediate mode GUI
CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG docking
)

# CPR - HTTP client library
CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.12.0
  OPTIONS
    "CPR_BUILD_TESTS OFF"
    "CPR_USE_SYSTEM_CURL OFF"
    "CPR_USE_SYSTEM_LIB_PSL ON"
    "CPR_FORCE_WINSSL_BACKEND ON"
    "CPR_ENABLE_CURL_HTTP_ONLY ON"
    "CURL_USE_LIBPSL OFF"
    "CURL_DISABLE_COOKIES ON"
    "BUILD_SHARED_LIBS OFF"
    "CURL_STATICLIB ON"
    "BUILD_CURL_EXE OFF"
    "BUILD_LIBCURL_DOCS OFF"
    "CMAKE_SKIP_INSTALL_RULES ON"
    "CURL_DISABLE_INSTALL ON"
)

# tomlplusplus - TOML parser
CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0
  DOWNLOAD_ONLY YES
)

# cpptrace - C++ stack trace library
CPMAddPackage(
  NAME cpptrace
  GITHUB_REPOSITORY jeremy-rifkin/cpptrace
  GIT_TAG v1.0.4
  OPTIONS
    "CPPTRACE_INSTALL OFF"
    "CPPTRACE_BUILD_TESTING OFF"
    "CPPTRACE_BUILD_EXAMPLES OFF"
    "CPPTRACE_DEMANGLE ON"
    "CPPTRACE_UNWIND_WITH_UNWINDLIB OFF"
    "CPPTRACE_UNWIND_WITH_EXECINFO OFF"
    "CPPTRACE_UNWIND_WITH_DBGHHELP ON"
    "CPPTRACE_SYMBOLIZE_WITH_DBGHELP ON"
    "CPPTRACE_DYNAMIC_LIB OFF"
    "CPPTRACE_STATIC_LIB ON"
)


if(BUILD_TESTS)
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.7.1
  )

  if(NOT Catch2_ADDED)
    find_package(Catch2 COMPONENTS Main QUIET)
    if(NOT Catch2_FOUND)
      message(WARNING "Catch2 was not added via CPM and not found by find_package(). Tests may fail to configure. \
To fix, ensure network access during configure, enable CPM local cache, or install Catch2 on the system.")
    endif()
  endif()
endif()

if(NOT SDL3_ADDED)
  message(FATAL_ERROR "Required dependency SDL3 was not acquired.")
endif()

if(NOT imgui_ADDED)
  message(FATAL_ERROR "Required dependency imgui was not acquired.")
endif()

if(NOT plog_ADDED)
  message(FATAL_ERROR "Required dependency plog was not acquired.")
endif()

if(NOT cpr_ADDED)
  message(FATAL_ERROR "Required dependency cpr was not acquired.")
endif()

if(NOT tomlplusplus_ADDED AND NOT tomlplusplus_SOURCE_DIR)
  message(FATAL_ERROR "Required dependency tomlplusplus was not acquired.")
endif()

# Setup ImGui library target
if(imgui_ADDED)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3)
endif()
