# Toolchain file for local LLVM-MinGW installation on Windows
# Usage: cmake --preset windows-mingw-release -DLLVM_MINGW_ROOT=/path/to/llvm-mingw

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Propagate LLVM_MINGW_ROOT to try_compile subprocesses (required for compiler tests)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

if(NOT LLVM_MINGW_ROOT)
    message(FATAL_ERROR "LLVM_MINGW_ROOT not set!")
endif()

set(TOOLCHAIN_ROOT "${LLVM_MINGW_ROOT}")

# Set the toolchain binaries
set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang++.exe")
set(CMAKE_RC_COMPILER  "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-windres.exe")
set(CMAKE_AR           "${TOOLCHAIN_ROOT}/bin/llvm-ar.exe")
set(CMAKE_RANLIB       "${TOOLCHAIN_ROOT}/bin/llvm-ranlib.exe")

# Set the sysroot to the toolchain directory
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}")

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Additional paths for finding libraries and includes
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}/x86_64-w64-mingw32")

# Ensure the toolchain bin directory is in the PATH for runtime
list(APPEND CMAKE_PREFIX_PATH "${TOOLCHAIN_ROOT}")
