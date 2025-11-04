#pragma once

#include "HookBase.hpp"
#include "HookCreateInfo.hpp"

#include <string>

namespace dqxclarity
{

/**
 * @brief Hook for capturing ephemeral HUD corner text
 */
class CornerTextHook : public HookBase
{
public:
    explicit CornerTextHook(const HookCreateInfo& create_info);
    ~CornerTextHook() = default;

    // Hook-specific polling
    bool PollCornerText();

    const std::string& GetLastText() const { return last_text_; }

protected:
    // HookBase pure virtual implementations
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;
    size_t ComputeStolenLength() override;

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxStringLength = 1024;
    static constexpr size_t kDefaultStolenBytes = 5;

    std::string last_text_;
};

} // namespace dqxclarity
