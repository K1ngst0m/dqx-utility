#pragma once

#include "../IConsoleSink.hpp"
#include <string>

namespace dqxclarity
{

class Console : public IConsoleSink
{
public:
    ~Console() override = default;

    void PrintDialog(const std::string&, const std::string&) override {}

    void PrintInfo(const std::string&) override {}
};

} // namespace dqxclarity
