#include "memory/MemoryFactory.hpp"
#include "memory/IProcessMemory.hpp"
#include "process/ProcessFinder.hpp"
#include "hooking/DialogHook.hpp"
#include "console/ConsoleFactory.hpp"
#include "../processing/Diagnostics.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>

void PrintUsage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "DQXClarity C++ - Dialog Text Extractor\n\n";
    std::cout << "Options:\n";
    std::cout << "  --version            Show version information\n";
    std::cout << "  --help               Show this help message\n";
    std::cout << "  --console            Print captured dialog to console (UTF-16)\n";
    std::cout << "  --verbose            Print detailed workflow info\n";
    std::cout << "\nBy default, dialog content is not printed. Use --console to enable output.\n";
}

void PrintVersion()
{
    std::cout << "DQXClarity C++ Dialog Extractor\n";
    std::cout << "Version: 1.0.0 (Development)\n";
    std::cout << "Platform: ";
#ifdef _WIN32
    std::cout << "Windows\n";
#else
    std::cout << "Linux\n";
#endif
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << "\n";
}

volatile bool g_running = true;

void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << "\n\nReceived interrupt signal. Shutting down...\n";
        g_running = false;
    }
}

int main(int argc, char* argv[])
{
    using namespace dqxclarity;

    std::cout << "DQXClarity C++ Dialog Extractor\n";
    std::cout << "================================\n\n";

    processing::Diagnostics::InitializeLogger();

    bool opt_console = false;
    bool opt_verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--version") == 0)
        {
            PrintVersion();
            return 0;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--console") == 0)
        {
            opt_console = true;
        }
        else if (strcmp(argv[i], "--verbose") == 0)
        {
            opt_verbose = true;
        }
    }

    processing::Diagnostics::SetVerbose(opt_verbose);

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Find DQXGame.exe
    if (opt_verbose)
        std::cout << "[1/3] Looking for DQXGame.exe...\n";
    auto pids = ProcessFinder::FindByName("DQXGame.exe", false);
    if (pids.empty())
    {
        std::cerr << "ERROR: DQXGame.exe not found!\n";
        std::cerr << "Make sure the game is running.\n";
        return 1;
    }
    if (opt_verbose)
        std::cout << "  Found PID: " << pids[0] << "\n\n";

    // Create memory interface
    if (opt_verbose)
        std::cout << "[2/3] Attaching to process...\n";
    auto memory_unique = MemoryFactory::CreatePlatformMemory();
    auto memory = std::shared_ptr<IProcessMemory>(std::move(memory_unique));

    if (!memory || !memory->AttachProcess(pids[0]))
    {
        std::cerr << "ERROR: Failed to attach to process!\n";
        std::cerr << "Make sure you're running as Administrator.\n";
        return 1;
    }
    if (opt_verbose)
        std::cout << "  Attached successfully\n\n";

    // Create and install dialog hook
    if (opt_verbose)
        std::cout << "[3/3] Installing dialog hook...\n";
    auto hook = std::make_unique<DialogHook>(memory);
    hook->SetVerbose(opt_verbose);
    hook->SetConsoleOutput(opt_console);
    hook->SetConsole(ConsoleFactory::Create(opt_console));
    if (!hook->InstallHook())
    {
        std::cerr << "ERROR: Failed to install dialog hook!\n";
        return 1;
    }
    if (opt_verbose)
        std::cout << "  Hook installed successfully\n\n";

    // Monitor for dialog
    if (opt_verbose || opt_console)
    {
        std::cout << "==========================================\n";
        std::cout << "Waiting for in-game dialog to appear...\n";
        std::cout << "Press Ctrl+C to exit.\n";
        std::cout << "==========================================\n\n";
    }

    int dialog_count = 0;

    while (g_running)
    {
        // Poll for new dialog data every 100ms
        if (hook->PollDialogData())
        {
            dialog_count++;
            if (opt_verbose)
            {
                std::cout << "[Dialog #" << dialog_count << "] captured\n";
            }
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean up
    if (opt_verbose)
        std::cout << "\nRemoving hook and cleaning up...\n";
    hook->RemoveHook();

    if (opt_verbose)
    {
        std::cout << "\nDialog extraction completed.\n";
        std::cout << "Total dialogs captured: " << dialog_count << "\n";
    }

    return 0;
}
