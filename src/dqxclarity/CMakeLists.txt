cmake_minimum_required(VERSION 3.20)

# dqxclarity subproject: static library + optional CLI tool
project(DQXClarity LANGUAGES CXX)

# libmem currently only supports MSVC on Windows
if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR "DQXClarity Windows builds currently require MSVC (MinGW support pending due to libmem dependency)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Library sources (shared across platforms)
set(DQXCLARITY_LIB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/MemoryFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/ProcessMemory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/Pattern.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/PatternScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/PatternFinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/ProcessMemoryScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/polling/PollingRunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/polling/PatternPollingTask.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/MemoryRegion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/signatures/Signatures.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/process/ProcessFinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/Codegen.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/HookBase.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/DialogHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/CornerTextHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/NetworkTextHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/PlayerHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/QuestHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/IntegrityHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/IntegrityMonitor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/MemoryPatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/HookRegistry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/HookManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/ScannerBase.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/ScannerManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/DialogScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/NoticeScreenScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/PostLoginScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scanning/PlayerNameScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/dqxclarity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/quest_message.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/ConsoleFactory.cpp
)

if(WIN32)
  list(APPEND DQXCLARITY_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/console/win/Console.cpp
  )
else()
  list(APPEND DQXCLARITY_LIB_SOURCES
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

if(PROFILING_LEVEL GREATER 0)
  target_compile_definitions(dqxclarity PUBLIC DQX_PROFILING_LEVEL=${PROFILING_LEVEL})
endif()

if(PROFILING_LEVEL GREATER_EQUAL 2)
  target_link_libraries(dqxclarity PUBLIC Tracy::TracyClient)
endif()

# Link libmem for cross-platform process memory operations
target_link_libraries(dqxclarity PUBLIC libmem)

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

  set_target_properties(dqxclarity-cpp PROPERTIES
    OUTPUT_NAME "dqxclarity-cpp"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/app"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/app"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/app"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/app"
  )
endif()


