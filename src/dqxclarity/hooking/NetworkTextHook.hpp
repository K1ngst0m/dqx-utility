#pragma once

#include "HookBase.hpp"
#include "HookCreateInfo.hpp"

#include <string>

namespace dqxclarity
{

/**
 * @brief Hook for capturing network text packets
 */
class NetworkTextHook : public HookBase
{
public:
    struct Capture
    {
        uintptr_t text_ptr = 0;
        uintptr_t category_ptr = 0;
        std::string category;
        std::string text;
        std::string category_raw_hex;
        std::string text_raw_hex;
        std::string category_strategy;
        std::string text_strategy;
    };

    explicit NetworkTextHook(const HookCreateInfo& create_info);
    ~NetworkTextHook() = default;

    // Hook-specific polling
    bool PollNetworkText();

    const Capture& GetLastCapture() const { return last_capture_; }

protected:
    // HookBase pure virtual implementations
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;
    size_t ComputeStolenLength() override;

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxCategoryLength = 128;
    static constexpr size_t kMaxTextLength = 2048;
    static constexpr size_t kDefaultStolenBytes = 5;
    static constexpr size_t kCategoryRegisterOffset = 4;  // EBX
    static constexpr size_t kTextRegisterOffset = 12;      // EDX

    Capture last_capture_;
};

} // namespace dqxclarity
