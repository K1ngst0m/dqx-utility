cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

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
)

set_target_properties(dqx_utility PROPERTIES OUTPUT_NAME "dqx-utility")
target_compile_features(dqx_utility PRIVATE cxx_std_20)
target_link_libraries(dqx_utility PRIVATE SDL3::SDL3-static imgui)
if(plog_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${plog_SOURCE_DIR}/include)
endif()

if(WIN32)
  target_compile_definitions(dqx_utility PRIVATE SDL_MAIN_USE_CONSOLE)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES src/main.cpp)
