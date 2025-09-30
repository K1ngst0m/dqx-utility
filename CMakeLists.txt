cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
    TLS_VERIFY ON)
endif()
include(${CPM_DOWNLOAD_LOCATION})

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

CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.12.0
  OPTIONS
    "CPR_BUILD_TESTS OFF"
    "CPR_BUILD_DOCS OFF"
    "CPR_USE_SYSTEM_CURL OFF"
)

CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0
  DOWNLOAD_ONLY YES
)

# Testing framework - only add if testing is enabled
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(BUILD_TESTS "Build tests" ON)
else()
  option(BUILD_TESTS "Build tests" OFF)
endif()

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
  target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
endif()


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
  src/UITheme.cpp
  src/ipc/TextSourceClient.cpp
  src/translate/ITranslator.hpp
  src/translate/OpenAITranslator.cpp
  src/translate/OpenAITranslator.hpp
  src/translate/GoogleTranslator.cpp
  src/translate/GoogleTranslator.hpp
  src/translate/TranslatorFactory.cpp
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
target_link_libraries(dqx_utility PRIVATE SDL3::SDL3-static imgui)
if(cpr_ADDED)
  target_link_libraries(dqx_utility PRIVATE cpr::cpr)
endif()
if(tomlplusplus_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${tomlplusplus_SOURCE_DIR}/include)
  if(NOT MSVC)
    target_compile_options(dqx_utility PRIVATE -Wno-deprecated-literal-operator)
  endif()
endif()
if(plog_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${plog_SOURCE_DIR}/include)
endif()

if(WIN32)
  target_compile_definitions(dqx_utility PRIVATE SDL_MAIN_USE_CONSOLE)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES src/main.cpp)

# Tests configuration
if(BUILD_TESTS AND Catch2_ADDED)
  enable_testing()
  
  # Create a library with the source code (excluding main.cpp) for testing
  add_library(dqx_utility_lib STATIC
    src/AppContext.cpp
    src/DialogWindow.cpp
    src/FontManager.cpp
    src/SettingsPanel.cpp
    src/WindowRegistry.cpp
    src/ProcessDetector.cpp
    src/WineDetector.cpp
    src/DQXClarityLauncher.cpp
    src/UITheme.cpp
    src/ipc/TextSourceClient.cpp
    src/translate/ITranslator.hpp
    src/translate/OpenAITranslator.cpp
    src/translate/OpenAITranslator.hpp
    src/translate/GoogleTranslator.cpp
    src/translate/GoogleTranslator.hpp
    src/translate/TranslatorFactory.cpp
    src/translate/LabelProcessor.cpp
    src/translate/LabelProcessor.hpp
    src/config/ConfigManager.cpp
    src/config/ConfigManager.hpp
  )
  
  # Apply same configuration as main executable
  target_compile_features(dqx_utility_lib PRIVATE cxx_std_20)
  target_link_libraries(dqx_utility_lib PRIVATE SDL3::SDL3-static imgui)
  if(cpr_ADDED)
    target_link_libraries(dqx_utility_lib PRIVATE cpr::cpr)
  endif()
  if(tomlplusplus_SOURCE_DIR)
    target_include_directories(dqx_utility_lib PRIVATE ${tomlplusplus_SOURCE_DIR}/include)
    if(NOT MSVC)
      target_compile_options(dqx_utility_lib PRIVATE -Wno-deprecated-literal-operator)
    endif()
  endif()
  if(plog_SOURCE_DIR)
    target_include_directories(dqx_utility_lib PRIVATE ${plog_SOURCE_DIR}/include)
  endif()
  
  if(MSVC)
    target_compile_options(dqx_utility_lib PRIVATE /W4 /permissive-)
  else()
    target_compile_options(dqx_utility_lib PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  
  # Test executable
  add_executable(dqx_utility_tests
    tests/test_main.cpp
    tests/utils/mock_http.cpp
    tests/translate/test_translator_factory.cpp
    tests/translate/test_openai_translator.cpp
    tests/translate/test_google_translator.cpp
  )
  
  target_include_directories(dqx_utility_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  
  target_link_libraries(dqx_utility_tests PRIVATE
    dqx_utility_lib
    Catch2::Catch2WithMain
  )
  
  # Register tests with CTest
  include(CTest)
  if(Catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(dqx_utility_tests)
  endif()
endif()
