#pragma once

#include "HookBase.hpp"
#include "HookCreateInfo.hpp"
#include "../console/IConsoleSink.hpp"

#include <string>

namespace dqxclarity
{

/**
 * @brief Hook for capturing NPC dialog and speaker names
 * 
 * Detour payload captures register state (ESI=text ptr, ESP+0x14=NPC ptr),
 * sets new data flag, then restores registers and executes stolen instructions.
 */
class DialogHook : public HookBase
{
public:
    explicit DialogHook(const HookCreateInfo& create_info);
    ~DialogHook() = default;

    // Hook-specific polling
    bool PollDialogData();

    std::string GetLastDialogText() const { return last_dialog_text_; }
    std::string GetLastNpcName() const { return last_npc_name_; }

    // Console output (legacy feature)
    void SetConsoleOutput(bool enabled) { console_output_ = enabled; }
    void SetConsole(ConsolePtr console) { console_ = std::move(console); }

protected:
    // HookBase pure virtual implementations
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;
    size_t ComputeStolenLength() override;

private:
    static constexpr size_t kMaxStringLength = 4096;
    static constexpr size_t kFlagOffset = 32;

    // Dialog data (mutable for const getter methods)
    mutable std::string last_dialog_text_;
    mutable std::string last_npc_name_;

    // Console output (optional legacy feature)
    bool console_output_ = false;
    ConsolePtr console_;
};

} // namespace dqxclarity
