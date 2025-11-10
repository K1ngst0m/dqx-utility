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
# Use prebuilt DLLs for Windows targets
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
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

    message(STATUS "Using prebuilt SDL3 for Windows from: ${SDL3_SOURCE_DIR}")
  endif()
else()
  # Build SDL3 from source for native Linux builds
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

  if(SDL3_ADDED)
    message(STATUS "Building SDL3 from source for Linux: ${SDL3_SOURCE_DIR}")
  endif()
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

# CPR - HTTP client library
CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.12.0
  OPTIONS
    "CPR_BUILD_TESTS OFF"
    "CPR_USE_SYSTEM_CURL OFF"
    "CPR_USE_SYSTEM_LIB_PSL ON"
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

# nlohmann/json - JSON parser for glossary system
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
  OPTIONS
    "JSON_BuildTests OFF"
    "JSON_Install OFF"
)

# miniz - ZIP extraction library for updater
CPMAddPackage(
  NAME miniz
  URL https://github.com/richgel999/miniz/releases/download/3.1.0/miniz-3.1.0.zip
  VERSION 3.1.0
)

# picosha2 - SHA-256 hash library for updater
CPMAddPackage(
  NAME picosha2
  GITHUB_REPOSITORY okdshin/PicoSHA2
  GIT_TAG b699e6c900be6e00152db5a3d123c1db42ea13d0
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

# libmem - Cross-platform process memory library (prebuilt binaries)
if(MSVC)
  CPMAddPackage(
    NAME libmem
    URL https://github.com/rdbo/libmem/releases/download/5.2.0-pre1/libmem-5.2.0-pre1-x86_64-windows-msvc-static-md.tar.gz
    DOWNLOAD_ONLY YES
  )

  if(libmem_ADDED)
    add_library(libmem STATIC IMPORTED GLOBAL)
    add_library(libmem::libmem ALIAS libmem)

    set_target_properties(libmem PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${libmem_SOURCE_DIR}/include"
      INTERFACE_COMPILE_DEFINITIONS "LM_EXPORT"
      INTERFACE_LINK_LIBRARIES "ntdll"
    )

    set_target_properties(libmem PROPERTIES
      IMPORTED_LOCATION_DEBUG "${libmem_SOURCE_DIR}/lib/debug/libmem.lib"
      IMPORTED_LOCATION_RELEASE "${libmem_SOURCE_DIR}/lib/release/libmem.lib"
      IMPORTED_LOCATION_RELWITHDEBINFO "${libmem_SOURCE_DIR}/lib/release/libmem.lib"
      IMPORTED_LOCATION_MINSIZEREL "${libmem_SOURCE_DIR}/lib/release/libmem.lib"
    )

    set_target_properties(libmem PROPERTIES
      IMPORTED_CONFIGURATIONS "RELEASE;DEBUG;RELWITHDEBINFO;MINSIZEREL"
    )

    message(STATUS "Using libmem prebuilt static library from: ${libmem_SOURCE_DIR}")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  CPMAddPackage(
    NAME libmem
    URL https://github.com/rdbo/libmem/releases/download/5.2.0-pre1/libmem-5.2.0-pre1-x86_64-linux-gnu-static.tar.gz
    DOWNLOAD_ONLY YES
  )

  if(libmem_ADDED)
    add_library(libmem STATIC IMPORTED GLOBAL)
    add_library(libmem::libmem ALIAS libmem)

    set_target_properties(libmem PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${libmem_SOURCE_DIR}/include"
      INTERFACE_COMPILE_DEFINITIONS "LM_EXPORT"
      INTERFACE_LINK_LIBRARIES "dl"
    )

    set_target_properties(libmem PROPERTIES
      IMPORTED_LOCATION "${libmem_SOURCE_DIR}/lib/liblibmem.a"
    )

    message(STATUS "Using libmem prebuilt static library from: ${libmem_SOURCE_DIR}")
  endif()
else()
  message(WARNING "libmem prebuilt binary only supports MSVC and Linux. MinGW builds will fail.")
  set(libmem_ADDED FALSE)
endif()

# utf8proc - Lightweight UTF-8 processing library
CPMAddPackage(
  NAME utf8proc
  GITHUB_REPOSITORY JuliaStrings/utf8proc
  GIT_TAG v2.11.0
  OPTIONS
    "UTF8PROC_ENABLE_TESTING OFF"
)

# rapidfuzz - Fast fuzzy string matching library
CPMAddPackage(
  NAME rapidfuzz
  URL https://github.com/rapidfuzz/rapidfuzz-cpp/archive/refs/tags/v3.3.3.zip
  OPTIONS
    "RAPIDFUZZ_INSTALL OFF"
    "RAPIDFUZZ_BUILD_TESTING OFF"
)

# Tracy - Real-time profiler (only for PROFILING_LEVEL >= 2)
if(PROFILING_LEVEL GREATER_EQUAL 2)
  CPMAddPackage(
    NAME tracy
    GITHUB_REPOSITORY wolfpld/tracy
    GIT_TAG v0.12.2
    OPTIONS
      "TRACY_ENABLE ON"
      "TRACY_ON_DEMAND ON"
      "TRACY_NO_FRAME_IMAGE ON"
  )
endif()


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
