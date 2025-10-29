#include "app/Application.hpp"
#include "updater/UpdateApplier.hpp"

#include <string>

int main(int argc, char** argv)
{
    // Check if launched in updater mode
    if (argc >= 4 && std::string(argv[1]) == "--updater-mode")
    {
        std::string packagePath = argv[2];
        std::string targetDir = argv[3];
        return updater::UpdateApplier::performUpdate(packagePath, targetDir) ? 0 : 1;
    }

    // Normal application mode
    return Application{argc, argv}.run();
}
