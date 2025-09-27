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


add_executable(dqx_utility
  src/main.cpp
)

set_target_properties(dqx_utility PROPERTIES OUTPUT_NAME "dqx-utility")
target_compile_features(dqx_utility PRIVATE cxx_std_20)
target_link_libraries(dqx_utility PRIVATE SDL3::SDL3-static)
if(plog_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${plog_SOURCE_DIR}/include)
endif()

if(WIN32)
  target_compile_definitions(dqx_utility PRIVATE SDL_MAIN_USE_CONSOLE)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES src/main.cpp)
