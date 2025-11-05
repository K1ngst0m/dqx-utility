#pragma once

#include "ScannerBase.hpp"

#include <string>
#include <chrono>

namespace dqxclarity
{

/**
 * @brief Dialog text scanner using direct memory reading
 *
 * Extracts dialog text from the game without using code hooks.
 * Searches for a specific byte pattern in non-executable memory regions
 * and reads dialog text via pointer dereferencing.
 */
class DialogScanner : public ScannerBase
{
public:
    explicit DialogScanner(const ScannerCreateInfo& create_info);
    ~DialogScanner() override = default;

    std::string GetLastDialogText() const { return last_dialog_text_; }
    std::string GetLastNpcName() const { return last_npc_name_; }

protected:
    bool OnInitialize() override;
    bool OnPoll() override;

private:
    static constexpr size_t kPointerOffset = 36;

    std::string last_dialog_text_;
    std::string last_npc_name_;
    std::chrono::steady_clock::time_point last_dialog_time_;
    static constexpr std::chrono::milliseconds kStateTimeout{ 5000 };
};

} // namespace dqxclarity

