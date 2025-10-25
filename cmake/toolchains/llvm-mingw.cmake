# Cross toolchain file for x86_64-w64-mingw32 using llvm-mingw container toolchain.
# Based on CMake cross-compiling docs and llvm-mingw naming conventions.
# Sources: CMake cross-compile docs; llvm-mingw README and community examples. (See citations.)

set(CMAKE_SYSTEM_NAME Windows)         # required for CMake cross builds. (cmake docs)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use the llvm-mingw clang drivers (documented names shipped by llvm-mingw images)
# The container image places the toolchain on PATH (see image metadata: /opt/llvm-mingw/bin).
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Prefer target sysroot for libs/includes only (common cross-build behavior)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Optional: If you want to force a specific sysroot inside the container add:
# set(CMAKE_SYSROOT /opt/llvm-mingw)   # uncomment only if your image uses /opt/llvm-mingw as root
