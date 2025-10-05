cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-Wno-deprecated-literal-operator" HAS_NO_DEPRECATED_LITERAL_OPERATOR)

# Build dqxclarity-cpp early (no external dependencies)
option(BUILD_DQXCLARITY "Build DQXClarity C++ executable" ON)

if(BUILD_DQXCLARITY)
  add_subdirectory(src/dqxclarity)
endif()

# Main application dependencies (SDL, ImGui, etc.)
option(BUILD_MAIN_APP "Build main dqx-utility application" ON)


# If main app is disabled, we still configure dependencies and tests; the app target is skipped later.

set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
    TLS_VERIFY ON)
endif()
include(${CPM_DOWNLOAD_LOCATION})

# Tests option defined early so tests can be configured before any early returns
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(BUILD_TESTS "Build tests" ON)
else()
  option(BUILD_TESTS "Build tests" OFF)
endif()

# Download prebuilt SDL3 for Windows MSVC
if(WIN32 AND MSVC)
  CPMAddPackage(
    NAME SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-devel-3.2.24-VC.zip
    DOWNLOAD_ONLY YES
  )
  
  if(SDL3_ADDED)
    # Set library paths based on architecture
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(SDL3_ARCH "x64")
    else()
      set(SDL3_ARCH "x86")
    endif()

    set(SDL3_LIB_DIR "${SDL3_SOURCE_DIR}/lib/${SDL3_ARCH}")
    set(SDL3_STATIC_LIB "${SDL3_LIB_DIR}/SDL3-static.lib")
    set(SDL3_IMPORT_LIB "${SDL3_LIB_DIR}/SDL3.lib")
    set(SDL3_DLL "${SDL3_LIB_DIR}/SDL3.dll")

    # Prefer static if available; otherwise use the import lib + DLL
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
  # Fallback to building from source on non-Windows or non-MSVC
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

CPMAddPackage(
  NAME plog
  GITHUB_REPOSITORY SergiusTheBest/plog
  GIT_TAG 1.1.10
  DOWNLOAD_ONLY YES
)

CPMAddPackage(
  NAME imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.3.tar.gz
  URL_HASH SHA256=9212ee7c4718b1466a5d99e64bce3ef1965704afea4ba651f8d978d0791b7c7c
)

if(BUILD_MAIN_APP)
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
)
endif()

CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0
  DOWNLOAD_ONLY YES
)

# Add Catch2 early so tests can be configured before optional early returns
if(BUILD_TESTS)
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.7.1
  )
endif()

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


# Tests configuration is placed before optional early return so tests can build even when app is skipped
if(BUILD_TESTS)
  enable_testing()
  
  # Test executable sources (exclude translator tests to avoid network deps)
  set(TEST_DQXCLARITY_SOURCES
    src/dqxclarity/memory/MemoryFactory.cpp
    src/dqxclarity/pattern/Pattern.cpp
    src/dqxclarity/pattern/PatternScanner.cpp
    src/dqxclarity/signatures/Signatures.cpp
    src/dqxclarity/process/ProcessFinder.cpp
  )
  
  if(WIN32)
    list(APPEND TEST_DQXCLARITY_SOURCES
      src/dqxclarity/memory/win/ProcessMemory.cpp
      src/dqxclarity/pattern/win/MemoryRegion.cpp
      src/dqxclarity/process/win/ProcessFinder.cpp
    )
  else()
    list(APPEND TEST_DQXCLARITY_SOURCES
      src/dqxclarity/memory/linux/ProcessMemory.cpp
      src/dqxclarity/pattern/linux/MemoryRegion.cpp
      src/dqxclarity/process/linux/ProcessFinder.cpp
    )
  endif()

  add_executable(dqx_utility_tests
    tests/test_main.cpp
    tests/dqxclarity/test_memory.cpp
    tests/dqxclarity/test_pattern_scanner.cpp
    tests/dqxclarity/test_process_finder.cpp
    ${TEST_DQXCLARITY_SOURCES}
  )
  
  target_include_directories(dqx_utility_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  
  target_link_libraries(dqx_utility_tests PRIVATE
    Catch2::Catch2WithMain
  )
  
  if(WIN32)
    target_link_libraries(dqx_utility_tests PRIVATE psapi)
  endif()
  
  # Register tests with CTest
  include(CTest)
  if(Catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(dqx_utility_tests)
  endif()
endif()

# Translator sources - always include network translators
set(MAIN_TRANSLATOR_SOURCES
  src/translate/ITranslator.hpp
  src/translate/OpenAITranslator.cpp
  src/translate/OpenAITranslator.hpp
  src/translate/GoogleTranslator.cpp
  src/translate/GoogleTranslator.hpp
  src/translate/TranslatorFactory.cpp
)

if(BUILD_MAIN_APP)
  add_executable(dqx_utility
    src/main.cpp
    src/AppContext.cpp
    src/DialogWindow.cpp
    src/FontManager.cpp
    src/SettingsPanel.cpp
    src/WindowRegistry.cpp
    src/ProcessDetector.cpp
    src/WineDetector.cpp
    src/DQXClarityLauncher.cpp
    src/DQXClarityService.cpp
    src/UITheme.cpp
    src/ipc/TextSourceClient.cpp
    ${MAIN_TRANSLATOR_SOURCES}
    src/translate/LabelProcessor.cpp
    src/translate/LabelProcessor.hpp
    src/config/ConfigManager.cpp
    src/config/ConfigManager.hpp
  )

  if(MSVC)
    target_compile_options(dqx_utility PRIVATE /W4 /permissive-)
  else()
    target_compile_options(dqx_utility PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  set_target_properties(dqx_utility PROPERTIES OUTPUT_NAME "dqx-utility")
  target_compile_features(dqx_utility PRIVATE cxx_std_20)
  target_link_libraries(dqx_utility PRIVATE SDL3::SDL3 imgui dqxclarity::dqxclarity)
  if(cpr_ADDED)
    target_link_libraries(dqx_utility PRIVATE cpr::cpr)
  endif()
  if(tomlplusplus_SOURCE_DIR)
    target_include_directories(dqx_utility PRIVATE ${tomlplusplus_SOURCE_DIR}/include)
    if(NOT MSVC AND HAS_NO_DEPRECATED_LITERAL_OPERATOR)
      target_compile_options(dqx_utility PRIVATE -Wno-deprecated-literal-operator)
    endif()
  endif()
  if(plog_SOURCE_DIR)
    target_include_directories(dqx_utility PRIVATE ${plog_SOURCE_DIR}/include)
    # Force UTF-8 narrow strings in plog on Windows
    if(WIN32)
      target_compile_definitions(dqx_utility PRIVATE PLOG_CHAR_IS_UTF8=1)
    endif()
  endif()

  if(WIN32)
    target_compile_definitions(dqx_utility PRIVATE SDL_MAIN_USE_CONSOLE)
  endif()

  # Ensure required runtime DLLs are next to the executable
  if(WIN32 AND MSVC)
    # Determine arch for the prebuilt SDL3 package
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(_SDL3_ARCH "x64")
    else()
      set(_SDL3_ARCH "x86")
    endif()

    # SDL3 (import library + DLL in lib/<arch>)
    if(DEFINED SDL3_SOURCE_DIR)
      set(_SDL3_DLL "${SDL3_SOURCE_DIR}/lib/${_SDL3_ARCH}/SDL3.dll")
      if(EXISTS "${_SDL3_DLL}")
        add_custom_command(TARGET dqx_utility POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_SDL3_DLL}" $<TARGET_FILE_DIR:dqx_utility>
          COMMENT "Copying SDL3.dll to dqx-utility output directory")
      else()
        message(WARNING "SDL3.dll not found at ${_SDL3_DLL}; dqx-utility may fail to start without it.")
      endif()
    endif()

  endif()
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES src/main.cpp)



