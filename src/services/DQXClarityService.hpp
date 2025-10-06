#pragma once

class DQXClarityLauncher;

// Simple global accessor so UI windows can fetch dialog messages from the
// DQXClarityLauncher without tight coupling.
DQXClarityLauncher* DQXClarityService_Get();
void DQXClarityService_Set(DQXClarityLauncher* launcher);
