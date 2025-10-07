# CompilerFlags.cmake
# Configures compiler flags and build options

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-deprecated-literal-operator" HAS_NO_DEPRECATED_LITERAL_OPERATOR)

function(set_project_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(configure_target_properties target)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "dqx-utility")
  target_compile_features(${target} PRIVATE cxx_std_20)
  
  if(WIN32)
    target_link_options(${target} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  endif()
endfunction()
