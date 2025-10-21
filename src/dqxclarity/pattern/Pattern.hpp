#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace dqxclarity
{

struct Pattern
{
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;

    static Pattern FromString(const std::string& pattern_str);

    static Pattern FromBytes(const uint8_t* data, size_t size);

    size_t Size() const { return bytes.size(); }

    bool IsValid() const { return !bytes.empty() && bytes.size() == mask.size(); }
};

} // namespace dqxclarity
