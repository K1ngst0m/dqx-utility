# InstallDependencies.cmake
# Manages runtime dependencies (DLLs, assets, etc.)

function(install_runtime_dependencies target)
  if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${target}>/assets"
      COMMENT "Copying assets directory"
    )
  endif()

  # Copy SDL3.dll for Windows targets
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND DEFINED SDL3_SOURCE_DIR)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(_SDL3_ARCH "x64")
    else()
      set(_SDL3_ARCH "x86")
    endif()

    set(_SDL3_DLL "${SDL3_SOURCE_DIR}/lib/${_SDL3_ARCH}/SDL3.dll")
    if(EXISTS "${_SDL3_DLL}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_SDL3_DLL}"
          "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying SDL3.dll"
      )
    else()
      message(WARNING "SDL3.dll not found at ${_SDL3_DLL}")
    endif()
  endif()
endfunction()
