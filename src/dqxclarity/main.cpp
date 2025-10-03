#include "memory/MemoryFactory.hpp"
#include "memory/IProcessMemory.hpp"
#include <iostream>
#include <string>
#include <cstring>

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "DQXClarity Memory Abstraction - C++ Implementation\n\n";
    std::cout << "Options:\n";
    std::cout << "  --version            Show version information\n";
    std::cout << "  --help               Show this help message\n";
    std::cout << "\nThis is a memory abstraction library for DQXClarity.\n";
    std::cout << "For testing, please use the unit test framework.\n";
}

void PrintVersion() {
    std::cout << "DQXClarity C++ Memory Abstraction\n";
    std::cout << "Version: 1.0.0 (Development)\n";
    std::cout << "Platform: ";
#ifdef _WIN32
    std::cout << "Windows\n";
#else
    std::cout << "Linux\n";
#endif
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "DQXClarity C++ Memory Abstraction\n";
    std::cout << "==================================\n\n";

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0) {
            PrintVersion();
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    return 0;
}