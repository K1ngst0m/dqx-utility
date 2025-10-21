#pragma once

#include "../IConsoleSink.hpp"
#include <windows.h>
#include <string>

namespace dqxclarity
{

class Console : public IConsoleSink
{
public:
    Console() = default;
    ~Console() override = default;

    void PrintDialog(const std::string& npc, const std::string& text) override;
    void PrintInfo(const std::string& line) override;

private:
    static std::wstring ToWide(const std::string& s);
};

} // namespace dqxclarity
