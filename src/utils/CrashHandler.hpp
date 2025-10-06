#pragma once

namespace utils {

class CrashHandler
{
public:
    static void Initialize();
    static void SetContext(const char* operation);
};

}
