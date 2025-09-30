#pragma once

#include <array>

struct IPCConfig
{
    static constexpr std::size_t PortfilePathSize = 512;

    std::array<char, PortfilePathSize> portfile_path{};
    bool auto_scroll_to_new = true;
};