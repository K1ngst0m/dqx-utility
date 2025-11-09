#include "app/Application.hpp"
#include "dqxclarity/hooking/HookGuardian.hpp"

#include <cstring>

int main(int argc, char** argv)
{
    // Guardian mode - minimal hook cleanup monitoring process
    if (argc == 2 && std::strcmp(argv[1], "--guardian-internal-mode") == 0)
    {
        return dqxclarity::persistence::HookGuardian::RunGuardianLoop();
    }
    
    // Normal application mode
    return Application{argc, argv}.run();
}
