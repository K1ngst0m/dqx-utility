#include "MemoryRegion.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace dqxclarity {

std::vector<MemoryRegion> MemoryRegionParser::ParseMaps(pid_t pid) {
    return ParseMapsFiltered(pid, false, false);
}

std::vector<MemoryRegion> MemoryRegionParser::ParseMapsFiltered(
    pid_t pid,
    bool require_readable,
    bool require_executable)
{
    std::vector<MemoryRegion> regions;
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps_file(maps_path);

    if (!maps_file.is_open()) {
        return regions;
    }

    std::string line;
    while (std::getline(maps_file, line)) {
        MemoryRegion region = ParseLine(line);

        if (require_readable && !region.IsReadable()) {
            continue;
        }
        if (require_executable && !region.IsExecutable()) {
            continue;
        }

        regions.push_back(region);
    }

    return regions;
}

MemoryRegion MemoryRegionParser::ParseLine(const std::string& line) {
    MemoryRegion region{};
    std::istringstream iss(line);
    std::string address_range, perms, offset, dev, inode;

    iss >> address_range >> perms >> offset >> dev >> inode;

    size_t dash_pos = address_range.find('-');
    if (dash_pos != std::string::npos) {
        region.start = std::stoull(address_range.substr(0, dash_pos), nullptr, 16);
        region.end = std::stoull(address_range.substr(dash_pos + 1), nullptr, 16);
    }

    region.protection = ParseProtection(perms);

    std::string pathname_part;
    while (iss >> pathname_part) {
        if (!region.pathname.empty()) {
            region.pathname += " ";
        }
        region.pathname += pathname_part;
    }

    return region;
}

int MemoryRegionParser::ParseProtection(const std::string& perm) {
    int protection = 0;

    if (perm.size() >= 3) {
        if (perm[0] == 'r') {
            protection |= static_cast<int>(MemoryProtection::Read);
        }
        if (perm[1] == 'w') {
            protection |= static_cast<int>(MemoryProtection::Write);
        }
        if (perm[2] == 'x') {
            protection |= static_cast<int>(MemoryProtection::Execute);
        }
    }

    return protection;
}

} // namespace dqxclarity
