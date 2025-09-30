#pragma once

#include <array>
#include <vector>

struct ContentState
{
    static constexpr std::size_t TitleBufferSize = 128;
    static constexpr std::size_t BodyBufferSize  = 1024;
    static constexpr std::size_t EntryBufferSize = 2048;

    std::vector<std::array<char, EntryBufferSize>> segments;
    std::array<char, EntryBufferSize> append_buffer{};
    int editing_index = -1;
    std::array<char, BodyBufferSize> edit_buffer{};
};