if(NOT WIN32)
  message(FATAL_ERROR "Packaging is only supported on Windows. Current platform: ${CMAKE_SYSTEM_NAME}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_OS_ARCH "windows-x64")
  set(_SDL3_ARCH "x64")
else()
  set(_OS_ARCH "windows-x86")
  set(_SDL3_ARCH "x86")
endif()

if(DEFINED SDL3_SOURCE_DIR)
  set(_SDL3_DLL_SRC "${SDL3_SOURCE_DIR}/lib/${_SDL3_ARCH}/SDL3.dll")
endif()

set(_PACK_EN_DIR    "${CMAKE_BINARY_DIR}/_portable_pack_en")
set(_PACK_ZHCN_DIR  "${CMAKE_BINARY_DIR}/_portable_pack_zh-CN")
set(_PACK_ALL_DIR   "${CMAKE_BINARY_DIR}/_portable_pack_all")

function(_stage_package STAGE_DIR LANG_TEMPLATE)
  add_custom_command(OUTPUT "${STAGE_DIR}/.staged"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:dqx_utility>" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${STAGE_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_SDL3_DLL_SRC}" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/${LANG_TEMPLATE}" "${STAGE_DIR}/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${STAGE_DIR}/.staged"
    DEPENDS dqx_utility
    COMMENT "Staging ${STAGE_DIR}"
  )
endfunction()

function(_stage_package_all STAGE_DIR)
  add_custom_command(OUTPUT "${STAGE_DIR}/.staged"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:dqx_utility>" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${STAGE_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_SDL3_DLL_SRC}" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/config.en.toml" "${STAGE_DIR}/config.en.toml"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/config.zh-CN.toml" "${STAGE_DIR}/config.zh-CN.toml"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/config.en.toml" "${STAGE_DIR}/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${STAGE_DIR}/.staged"
    DEPENDS dqx_utility
    COMMENT "Staging all-language package"
  )
endfunction()

_stage_package("${_PACK_EN_DIR}"   "config.en.toml")
_stage_package("${_PACK_ZHCN_DIR}" "config.zh-CN.toml")
_stage_package_all("${_PACK_ALL_DIR}")

add_custom_target(package_en
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_EN_DIR}" 
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-en-${PROJECT_VERSION}-${_OS_ARCH}.zip" 
    --format=zip dqx-utility.exe SDL3.dll config.toml assets/
  DEPENDS "${_PACK_EN_DIR}/.staged"
  COMMENT "Packaging English ZIP"
)

add_custom_target(package_zh_CN
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_ZHCN_DIR}" 
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-zh-CN-${PROJECT_VERSION}-${_OS_ARCH}.zip" 
    --format=zip dqx-utility.exe SDL3.dll config.toml assets/
  DEPENDS "${_PACK_ZHCN_DIR}/.staged"
  COMMENT "Packaging Chinese ZIP"
)

add_custom_target(package_all_lang
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_ALL_DIR}" 
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-all-${PROJECT_VERSION}-${_OS_ARCH}.zip" 
    --format=zip dqx-utility.exe SDL3.dll config.toml config.en.toml config.zh-CN.toml assets/
  DEPENDS "${_PACK_ALL_DIR}/.staged"
  COMMENT "Packaging all-language ZIP"
)

add_custom_target(package_all DEPENDS package_en package_zh_CN package_all_lang)
