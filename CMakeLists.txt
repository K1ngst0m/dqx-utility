cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.3 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(PROFILING_LEVEL "1" CACHE STRING "Profiling level: 0=disabled, 1=timer+plog, 2=tracy+timer")
set_property(CACHE PROFILING_LEVEL PROPERTY STRINGS "0" "1" "2")
if(NOT PROFILING_LEVEL MATCHES "^[0-2]$")
  message(FATAL_ERROR "PROFILING_LEVEL must be 0, 1, or 2 (got: ${PROFILING_LEVEL})")
endif()

option(BUILD_DQXCLARITY_CLI "Build DQXClarity C++ CLI executable" OFF)
option(ENABLE_DEBUG_SECTION "Enable debugging sections in the UI" OFF)

include(CompilerOptions)
include(ClangFormat)
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
  enable_testing()
  add_subdirectory(tests)
endif()

# Include packaging for Windows targets
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  include(Packaging)
endif()
