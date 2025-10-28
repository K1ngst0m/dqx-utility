function(generate_update_manifest STAGE_DIR VERSION OUTPUT_FILE IS_RELEASE)
    set(MANIFEST_JSON "{\"version\":\"${VERSION}\",\"is_release\":${IS_RELEASE},\"files\":[")
    set(FIRST_FILE TRUE)

    file(GLOB_RECURSE ALL_FILES
        RELATIVE "${STAGE_DIR}"
        "${STAGE_DIR}/*"
    )

    list(FILTER ALL_FILES EXCLUDE REGEX "^manifest\\.json$")
    list(FILTER ALL_FILES EXCLUDE REGEX "^\\.staged$")

    foreach(REL_PATH ${ALL_FILES})
        file(SHA256 "${STAGE_DIR}/${REL_PATH}" FILE_SHA)

        if(REL_PATH STREQUAL "config.toml")
            set(ACTION "preserve")
        else()
            set(ACTION "replace")
        endif()

        if(NOT FIRST_FILE)
            string(APPEND MANIFEST_JSON ",")
        endif()
        set(FIRST_FILE FALSE)

        string(REPLACE "\\" "/" JSON_PATH "${REL_PATH}")
        string(APPEND MANIFEST_JSON "{\"path\":\"${JSON_PATH}\",\"sha256\":\"${FILE_SHA}\",\"action\":\"${ACTION}\"}")
    endforeach()

    string(APPEND MANIFEST_JSON "]}")

    file(WRITE "${OUTPUT_FILE}" "${MANIFEST_JSON}")
    message(STATUS "Generated manifest: ${OUTPUT_FILE}")

    list(LENGTH ALL_FILES FILE_COUNT)
    message(STATUS "  - Manifest contains ${FILE_COUNT} files")
endfunction()

# Script mode: execute if variables are defined
if(DEFINED STAGE_DIR AND DEFINED VERSION AND DEFINED OUTPUT_FILE)
    if(NOT DEFINED IS_RELEASE)
        set(IS_RELEASE "false")
    endif()
    generate_update_manifest("${STAGE_DIR}" "${VERSION}" "${OUTPUT_FILE}" "${IS_RELEASE}")
endif()
