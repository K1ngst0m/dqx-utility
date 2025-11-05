#pragma once

#include "HookBase.hpp"
#include <mutex>

namespace dqxclarity
{

/**
 * @brief Hook for game's integrity check function
 * 
 * Patches the anti-cheat integrity check to allow other hooks to temporarily
 * restore original bytes during integrity scans, then re-apply patches afterward.
 */
class IntegrityHook : public HookBase
{
public:
    struct RestoreSite
    {
        uintptr_t address;
        std::vector<uint8_t> bytes;
    };

    explicit IntegrityHook(const HookCreateInfo& create_info);
    ~IntegrityHook() override;

    // IHook interface (via HookBase)
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;

    // Integrity-specific accessors
    uintptr_t GetStateAddress() const { return state_address_; }

    // Restore site management for IntegrityMonitor
    void AddRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes);
    void UpdateRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes);
    void MoveRestoreTarget(uintptr_t old_address, uintptr_t new_address, const std::vector<uint8_t>& original_bytes);
    std::vector<RestoreSite> GetRestoreSites() const;

    // Configuration
    void SetDiagnosticsEnabled(bool enabled) { diagnostics_enabled_ = enabled; }

protected:
    // Override stolen length computation
    size_t ComputeStolenLength() override;

private:
    uintptr_t state_address_ = 0;
    std::vector<RestoreSite> restore_sites_;
    mutable std::mutex restore_mutex_;
    bool diagnostics_enabled_ = false;

    // Instruction decoding for safe stolen byte calculation
    static size_t DecodeInstrLen(const uint8_t* p, size_t max);
    static bool HasPcRelativeBranch(const uint8_t* data, size_t n);
};

} // namespace dqxclarity

