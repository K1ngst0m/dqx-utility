cmake_minimum_required(VERSION 3.20)

# dqxclarity subproject: static library + optional CLI tool
project(DQXClarity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Library sources (shared across platforms)
set(DQXCLARITY_LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/MemoryFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/Pattern.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/PatternScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/PatternFinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/signatures/Signatures.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/process/ProcessFinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/process/NoticeWaiter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/process/PostLoginDetector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/Codegen.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/DialogHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/CornerTextHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/QuestHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/IntegrityDetour.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/IntegrityMonitor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/MemoryPatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/dqxclarity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/quest_message.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/ConsoleFactory.cpp
)

# Platform-specific library sources
if(WIN32)
  list(APPEND DQXCLARITY_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/win/ProcessMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pattern/win/MemoryRegion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process/win/ProcessFinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/win/Console.cpp
  )
else()
  list(APPEND DQXCLARITY_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/linux/ProcessMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pattern/linux/MemoryRegion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process/linux/ProcessFinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/linux/Console.cpp
  )
endif()

# Static library target
add_library(dqxclarity STATIC ${DQXCLARITY_LIB_SOURCES})
add_library(dqxclarity::dqxclarity ALIAS dqxclarity)

target_include_directories(dqxclarity
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
  target_compile_definitions(dqxclarity PRIVATE PLOG_CHAR_IS_UTF8=1)
endif()

target_compile_features(dqxclarity PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(dqxclarity PRIVATE /W4 /permissive-)
else()
  target_compile_options(dqxclarity PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_PROFILING)
  target_link_libraries(dqxclarity PUBLIC Tracy::TracyClient)
  target_compile_definitions(dqxclarity PUBLIC DQX_ENABLE_PROFILING)
endif()

if(WIN32)
  target_link_libraries(dqxclarity PUBLIC psapi)
endif()

# Optional CLI tool built on the library (for development)
if(BUILD_DQXCLARITY_CLI)
  add_executable(dqxclarity-cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

  target_include_directories(dqxclarity-cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  target_compile_features(dqxclarity-cpp PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(dqxclarity-cpp PRIVATE /W4 /permissive-)
  else()
    target_compile_options(dqxclarity-cpp PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  target_link_libraries(dqxclarity-cpp PRIVATE dqxclarity)

  set_target_properties(dqxclarity-cpp PROPERTIES OUTPUT_NAME "dqxclarity-cpp")
endif()
