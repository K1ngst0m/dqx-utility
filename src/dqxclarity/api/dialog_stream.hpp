#pragma once

#include <cstdint>
#include <string>

namespace dqxclarity
{

enum class DialogStreamType
{
    Dialog,
    CornerText
};

struct DialogStreamItem
{
    std::uint64_t seq = 0;
    DialogStreamType type = DialogStreamType::Dialog;
    std::string text;
    std::string speaker;
};

} // namespace dqxclarity
