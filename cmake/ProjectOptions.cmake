set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-deprecated-literal-operator" HAS_NO_DEPRECATED_LITERAL_OPERATOR)

add_library(project_options INTERFACE)
add_library(project_warnings INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(project_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_PROFILING)
  target_compile_definitions(project_options INTERFACE DQX_ENABLE_PROFILING)
endif()

if(WIN32)
  target_compile_definitions(project_options INTERFACE PLOG_CHAR_IS_UTF8=1)
endif()

function(configure_target_properties target)
  get_target_property(target_type ${target} TYPE)

  if(NOT target_type)
    message(FATAL_ERROR "Target ${target} has no TYPE property")
  endif()

  if(target_type STREQUAL "EXECUTABLE")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "dqx-utility")

    # Set output directory to {preset}/{Config}/app/
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/app"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/app"
      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/app"
      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/app"
    )

    if(WIN32)
      target_link_options(${target} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
  endif()
endfunction()