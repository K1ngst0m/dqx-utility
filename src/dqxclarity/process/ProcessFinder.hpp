#pragma once

#include <libmem/libmem.hpp>
#include <vector>
#include <string>
#include <optional>

namespace dqxclarity
{

struct ProcessInfo
{
    libmem::Pid pid;
    std::string name;
    std::string exe_path;
    bool is_wine_process;
};

class ProcessFinder
{
public:
    static std::vector<ProcessInfo> FindAll();

    static std::vector<libmem::Pid> FindByName(const std::string& name, bool case_sensitive = false);

    static std::vector<libmem::Pid> FindByExePath(const std::string& path);

    static std::optional<ProcessInfo> GetProcessInfo(libmem::Pid pid);

    static bool IsWineProcess(libmem::Pid pid);

private:
    static std::string ToLower(const std::string& str);
};

} // namespace dqxclarity
