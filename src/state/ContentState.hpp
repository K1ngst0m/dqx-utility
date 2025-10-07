#pragma once

#include <array>
#include <vector>
#include <string>

struct ContentState
{
    static constexpr std::size_t TitleBufferSize = 128;
    static constexpr std::size_t BodyBufferSize  = 1024;
    static constexpr std::size_t EntryBufferSize = 2048;

    std::vector<std::array<char, EntryBufferSize>> segments;
    std::vector<std::string> speakers;  // NPC names parallel to segments
    std::array<char, EntryBufferSize> append_buffer{};
    int editing_index;
    std::array<char, BodyBufferSize> edit_buffer{};

    void applyDefaults()
    {
        segments.clear();
        segments.emplace_back();
        segments.back().fill('\0');
        speakers.clear();
        speakers.emplace_back();
        
        append_buffer.fill('\0');
        editing_index = -1;
        edit_buffer.fill('\0');
    }
};
