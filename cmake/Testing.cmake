# Testing.cmake
# Test configuration and setup

if(NOT BUILD_TESTS)
  return()
endif()

enable_testing()

# Test source files (only tests; production code should be provided by library targets)
set(TEST_SOURCES
  tests/test_main.cpp
  tests/test_text_processing.cpp
  tests/dqxclarity/test_memory.cpp
  tests/dqxclarity/test_pattern_scanner.cpp
  tests/dqxclarity/test_process_finder.cpp
)

add_executable(dqx_utility_tests
  ${TEST_SOURCES}
)

target_include_directories(dqx_utility_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link the test executable against Catch2 and the production libraries (do not duplicate sources)
target_link_libraries(dqx_utility_tests PRIVATE Catch2::Catch2WithMain dqx_core dqxclarity::dqxclarity)

if(WIN32)
  target_link_libraries(dqx_utility_tests PRIVATE psapi)
endif()

# Provide a 'check' target that runs the tests via CTest
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS dqx_utility_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

include(CTest)
if(Catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(dqx_utility_tests)
endif()
