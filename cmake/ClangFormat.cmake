# ClangFormat.cmake
# Adds a CMake target to format all C++ source files with clang-format

# Find clang-format executable from system PATH
find_program(CLANG_FORMAT_EXECUTABLE
    NAMES
        clang-format
        clang-format-18
        clang-format-17
        clang-format-16
        clang-format-15
        clang-format-14
    DOC "Path to clang-format executable"
)

if(CLANG_FORMAT_EXECUTABLE)
    message(STATUS "clang-format found: ${CLANG_FORMAT_EXECUTABLE}")

    # Collect all source files from src/ and tests/ directories
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_SOURCE_DIR}/tests/*.hpp"
    )

    # Add a target to format all files
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE}
        -i
        -style=file
        ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on all source files in src/ and tests/"
        VERBATIM
    )

    # Add a target to check formatting without modifying files
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE}
        --dry-run
        --Werror
        -style=file
        ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking clang-format on all source files in src/ and tests/"
        VERBATIM
    )

    message(STATUS "Added targets: 'format' (apply formatting) and 'format-check' (check formatting)")
else()
    message(WARNING "clang-format not found. Format targets will not be available.")
endif()
