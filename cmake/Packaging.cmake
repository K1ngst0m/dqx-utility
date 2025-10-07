# Packaging.cmake
# CPack configuration for creating distributable packages

if(NOT WIN32)
  message(FATAL_ERROR "Packaging is only supported on Windows. Current platform: ${CMAKE_SYSTEM_NAME}")
endif()

set(CPACK_PACKAGE_NAME "dqx-utility")
set(CPACK_PACKAGE_VENDOR "DQX Utility")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dragon Quest X Utility - Portable Windows Application")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64-portable")

set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_INSTALL_CMAKE_PROJECTS "")

set(_PACK_DIR "${CMAKE_BINARY_DIR}/_portable_pack")

add_custom_target(prepare_package ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${_PACK_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_PACK_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:dqx_utility>" "${_PACK_DIR}/"
  DEPENDS dqx_utility
  COMMENT "Preparing portable package directory"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
  add_custom_command(TARGET prepare_package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
      "${CMAKE_SOURCE_DIR}/assets" 
      "${_PACK_DIR}/assets"
  )
endif()

if(DEFINED SDL3_SOURCE_DIR)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_SDL3_ARCH "x64")
  else()
    set(_SDL3_ARCH "x86")
  endif()
  
  set(_SDL3_DLL_SRC "${SDL3_SOURCE_DIR}/lib/${_SDL3_ARCH}/SDL3.dll")
  if(EXISTS "${_SDL3_DLL_SRC}")
    add_custom_command(TARGET prepare_package POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy "${_SDL3_DLL_SRC}" "${_PACK_DIR}/"
    )
  endif()
endif()

set(CPACK_INSTALLED_DIRECTORIES "${_PACK_DIR};.")

include(CPack)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_OS_ARCH "windows-x64")
else()
  set(_OS_ARCH "windows-x86")
endif()

set(_PACK_EN_DIR    "${CMAKE_BINARY_DIR}/_portable_pack_en")
set(_PACK_ZHCN_DIR  "${CMAKE_BINARY_DIR}/_portable_pack_zh-CN")

function(_stage_package STAGE_DIR LANG_TEMPLATE)
  add_custom_command(OUTPUT "${STAGE_DIR}/.staged"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:dqx_utility>" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${STAGE_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:dqx_utility>/SDL3.dll" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/${LANG_TEMPLATE}" "${STAGE_DIR}/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${STAGE_DIR}/.staged"
    DEPENDS dqx_utility
    COMMENT "Staging ${STAGE_DIR}"
  )
endfunction()

_stage_package("${_PACK_EN_DIR}"   "config.en.toml")
_stage_package("${_PACK_ZHCN_DIR}" "config.zh-CN.toml")

add_custom_target(package_en
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_EN_DIR}" 
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-en-${PROJECT_VERSION}-${_OS_ARCH}.zip" 
    --format=zip dqx-utility.exe SDL3.dll config.toml assets/
  DEPENDS "${_PACK_EN_DIR}/.staged"
  COMMENT "Packaging EN ZIP"
)

add_custom_target(package_zh_CN
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_ZHCN_DIR}" 
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-zh-CN-${PROJECT_VERSION}-${_OS_ARCH}.zip" 
    --format=zip dqx-utility.exe SDL3.dll config.toml assets/
  DEPENDS "${_PACK_ZHCN_DIR}/.staged"
  COMMENT "Packaging zh-CN ZIP"
)

add_custom_target(package_all DEPENDS package_en package_zh_CN)
