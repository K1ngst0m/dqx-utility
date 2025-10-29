if(NOT WIN32)
  message(FATAL_ERROR "Packaging is only supported on Windows. Current platform: ${CMAKE_SYSTEM_NAME}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_OS_ARCH "windows-x64")
else()
  set(_OS_ARCH "windows-x86")
endif()

include(GenerateManifest)

set(_PACK_DIR    "${CMAKE_BINARY_DIR}/_portable_pack")
set(_PACK_ZHCN_DIR  "${CMAKE_BINARY_DIR}/_portable_pack_zh-CN")

function(_stage_package STAGE_DIR LANG_TEMPLATE)
  add_custom_command(OUTPUT "${STAGE_DIR}/.staged"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:dqxu_app>" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${STAGE_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3>" "${STAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/templates/${LANG_TEMPLATE}" "${STAGE_DIR}/config.toml"
    COMMAND ${CMAKE_COMMAND}
      -DSTAGE_DIR="${STAGE_DIR}"
      -DVERSION="${PROJECT_VERSION}"
      -DOUTPUT_FILE="${STAGE_DIR}/manifest.json"
      -DIS_RELEASE="true"
      -DCMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
      -P "${CMAKE_SOURCE_DIR}/cmake/GenerateManifest.cmake"
    COMMAND ${CMAKE_COMMAND} -E touch "${STAGE_DIR}/.staged"
    DEPENDS dqxu_app
    COMMENT "Staging ${STAGE_DIR}"
  )
endfunction()

_stage_package("${_PACK_DIR}"   "config.en.toml")
_stage_package("${_PACK_ZHCN_DIR}" "config.zh-CN.toml")

add_custom_target(package
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_DIR}"
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-${PROJECT_VERSION}-${_OS_ARCH}.zip"
    --format=zip dqx-utility.exe SDL3.dll config.toml manifest.json assets/
  DEPENDS "${_PACK_DIR}/.staged"
  COMMENT "Packaging Default ZIP"
)

add_custom_target(package_zh_CN
  COMMAND ${CMAKE_COMMAND} -E chdir "${_PACK_ZHCN_DIR}"
    ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dqx-utility-zh-CN-${PROJECT_VERSION}-${_OS_ARCH}.zip"
    --format=zip dqx-utility.exe SDL3.dll config.toml manifest.json assets/
  DEPENDS "${_PACK_ZHCN_DIR}/.staged"
  COMMENT "Packaging Chinese ZIP"
)