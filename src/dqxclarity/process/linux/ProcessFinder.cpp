#include "../ProcessFinder.hpp"
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

namespace dqxclarity {

std::vector<pid_t> ProcessFinder::EnumerateProcesses() {
    std::vector<pid_t> pids;
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) {
        return pids;
    }

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        bool is_numeric = true;
        for (const char* p = entry->d_name; *p; ++p) {
            if (!std::isdigit(*p)) { is_numeric = false; break; }
        }
        if (is_numeric && entry->d_name[0] != '\0') {
            pid_t pid = std::stoi(entry->d_name);
            pids.push_back(pid);
        }
    }
    closedir(proc_dir);
    return pids;
}

std::string ProcessFinder::ReadProcFile(pid_t pid, const std::string& filename) {
    std::string path = "/proc/" + std::to_string(pid) + "/" + filename;
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer; buffer << file.rdbuf();
    return buffer.str();
}

std::string ProcessFinder::GetProcessName(pid_t pid) {
    std::string comm = ReadProcFile(pid, "comm");
    if (!comm.empty() && comm.back() == '\n') comm.pop_back();
    return comm;
}

std::string ProcessFinder::GetProcessExePath(pid_t pid) {
    std::string exe_link = "/proc/" + std::to_string(pid) + "/exe";
    char exe_path[4096];
    ssize_t len = readlink(exe_link.c_str(), exe_path, sizeof(exe_path) - 1);
    if (len == -1) return "";
    exe_path[len] = '\0';
    return std::string(exe_path);
}

bool ProcessFinder::IsWineProcess(pid_t pid) {
    std::string exe_path = GetProcessExePath(pid);
    if (exe_path.find("wine") != std::string::npos) return true;
    std::string environ_path = "/proc/" + std::to_string(pid) + "/environ";
    std::ifstream environ_file(environ_path);
    if (!environ_file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(environ_file)), std::istreambuf_iterator<char>());
    return content.find("WINEPREFIX") != std::string::npos || content.find("WINEDEBUG") != std::string::npos;
}

} // namespace dqxclarity
