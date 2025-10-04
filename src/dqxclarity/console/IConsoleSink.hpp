#pragma once

#include <string>
#include <memory>

namespace dqxclarity {

class IConsoleSink {
public:
    virtual ~IConsoleSink() = default;
    virtual void PrintDialog(const std::string& npc, const std::string& text) = 0;
    virtual void PrintInfo(const std::string& line) = 0;
};

using ConsolePtr = std::shared_ptr<IConsoleSink>;

} // namespace dqxclarity