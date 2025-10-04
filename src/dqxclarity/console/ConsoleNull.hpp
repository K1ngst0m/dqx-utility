#pragma once

#include "IConsoleSink.hpp"

namespace dqxclarity {

class ConsoleNull : public IConsoleSink {
public:
    ~ConsoleNull() override = default;
    void PrintDialog(const std::string&, const std::string&) override {}
    void PrintInfo(const std::string&) override {}
};

} // namespace dqxclarity