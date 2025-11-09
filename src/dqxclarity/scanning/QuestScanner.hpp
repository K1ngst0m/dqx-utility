#pragma once

#include "ScannerBase.hpp"

#include <string>
#include <chrono>

namespace dqxclarity
{

class QuestScanner : public ScannerBase
{
public:
    explicit QuestScanner(const ScannerCreateInfo& create_info);
    ~QuestScanner() override = default;

    std::string GetLastSubquestName() const { return last_subquest_name_; }
    std::string GetLastQuestName() const { return last_quest_name_; }
    std::string GetLastDescription() const { return last_description_; }

protected:
    bool OnInitialize() override;
    bool OnPoll() override;

private:
    Pattern selected_pattern_{};
    uint32_t name_offset_ = 0;
    uint32_t subname_offset_ = 0;
    uint32_t description_offset_ = 0;

    std::string last_subquest_name_;
    std::string last_quest_name_;
    std::string last_description_;
    std::chrono::steady_clock::time_point last_time_{};
    static constexpr std::chrono::milliseconds kStateTimeout{ 5000 };
};

} // namespace dqxclarity
