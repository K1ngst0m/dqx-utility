enable_testing()

add_executable(dqxu_tests
  test_main.cpp
  test_text_processing.cpp
  dqxclarity/test_memory.cpp
  dqxclarity/test_pattern_scanner.cpp
  dqxclarity/test_process_finder.cpp
  dqxclarity/test_hook_registry.cpp
)

# Set output directory to {preset}/{Config}/tests/
set_target_properties(dqxu_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/tests"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/tests"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/tests"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/tests"
)

target_include_directories(dqxu_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(dqxu_tests
  PRIVATE
    Catch2::Catch2WithMain
    dqxu_core
    dqxclarity::dqxclarity
)

if(WIN32)
  target_link_libraries(dqxu_tests PRIVATE psapi)
endif()

add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS dqxu_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

include(CTest)
if(TARGET Catch2::Catch2)
  if(Catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
  endif()
  include(Catch)
  # Skip test discovery when cross-compiling (can't execute Windows .exe on Linux)
  if(NOT CMAKE_CROSSCOMPILING)
    catch_discover_tests(dqxu_tests)
  endif()
endif()