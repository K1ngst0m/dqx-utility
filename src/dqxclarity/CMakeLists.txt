cmake_minimum_required(VERSION 3.20)

# Standalone subproject for DQXClarity C++ tool
project(DQXClarityCpp LANGUAGES CXX)

# Match parent project's C++ standard and flags as closely as possible
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sources (shared across platforms)
set(DQXCLARITY_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/MemoryFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/Pattern.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/PatternScanner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/signatures/Signatures.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/process/ProcessFinder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hooking/DialogHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/IConsoleSink.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/ConsoleFactory.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/ConsoleFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/console/ConsoleNull.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/IProcessMemory.hpp
)

# Platform-specific sources
if(WIN32)
  list(APPEND DQXCLARITY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/win/ProcessMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pattern/win/MemoryRegion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process/win/ProcessFinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/win/Console.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/win/Console.hpp
  )
else()
  list(APPEND DQXCLARITY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/linux/ProcessMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pattern/linux/MemoryRegion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process/linux/ProcessFinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/linux/Console.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/linux/Console.hpp
  )
endif()

add_executable(dqxclarity-cpp ${DQXCLARITY_SOURCES})

# Include path root
target_include_directories(dqxclarity-cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Match compile features and options
target_compile_features(dqxclarity-cpp PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(dqxclarity-cpp PRIVATE /W4 /permissive-)
else()
  target_compile_options(dqxclarity-cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Link system libs
if(WIN32)
  target_link_libraries(dqxclarity-cpp PRIVATE psapi)
endif()

# Output name mirrors parent
set_target_properties(dqxclarity-cpp PROPERTIES OUTPUT_NAME "dqxclarity-cpp")
