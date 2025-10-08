# Testing.cmake
# Test configuration and setup

if(NOT BUILD_TESTS)
  return()
endif()

enable_testing()

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
  tests/test_text_processing.cpp
  tests/dqxclarity/test_memory.cpp
  tests/dqxclarity/test_pattern_scanner.cpp
  tests/dqxclarity/test_process_finder.cpp
  ${TEST_DQXCLARITY_SOURCES}
)

target_include_directories(dqx_utility_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dqx_utility_tests PRIVATE Catch2::Catch2WithMain)

if(WIN32)
  target_link_libraries(dqx_utility_tests PRIVATE psapi)
endif()

include(CTest)
if(Catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(dqx_utility_tests)
endif()
