#pragma once

#ifdef _WIN32
#include <windows.h>
using pid_t = DWORD;
#else
#include <sys/types.h>
#endif

namespace dqxclarity
{

using ProcessId = pid_t;

} // namespace dqxclarity
