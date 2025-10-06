#include "DQXClarityService.hpp"

static DQXClarityLauncher* g_dqxc_launcher = nullptr;

DQXClarityLauncher* DQXClarityService_Get() { return g_dqxc_launcher; }
void DQXClarityService_Set(DQXClarityLauncher* launcher) { g_dqxc_launcher = launcher; }
