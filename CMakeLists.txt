cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_PROFILING "Enable profiling instrumentation" OFF)
option(BUILD_DQXCLARITY_CLI "Build DQXClarity C++ CLI executable" OFF)
option(ENABLE_DEBUG_SECTION "Enable debugging sections in the UI" OFF)

include(ProjectOptions)
add_subdirectory(src/dqxclarity)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(BUILD_TESTS "Build tests" ON)
else()
  option(BUILD_TESTS "Build tests" OFF)
endif()

include(Dependencies)
include(InstallDependencies)

add_subdirectory(src)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(WIN32)
  include(Packaging)
endif()