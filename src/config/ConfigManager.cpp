#include "ConfigManager.hpp"

#include "StateSerializer.hpp"
#include "WindowStateApplier.hpp"
#include "../ui/WindowRegistry.hpp"
#include "../ui/dialog/DialogWindow.hpp"
#include "../ui/quest/QuestWindow.hpp"
#include "../ui/quest/QuestHelperWindow.hpp"
#include "../ui/Localization.hpp"
#include "../ui/common/BaseWindowState.hpp"
#include "../ui/dialog/DialogStateManager.hpp"
#include "../ui/quest/QuestStateManager.hpp"
#include "../ui/quest/QuestHelperStateManager.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../processing/Diagnostics.hpp"
#include "../utils/Profile.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <imgui.h>

namespace fs = std::filesystem;

namespace
{
/// Safe string copy with guaranteed null termination
inline void safe_strncpy(char* dest, const char* src, size_t dest_size)
{
    if (dest_size == 0)
        return;
#ifdef _WIN32
    strncpy_s(dest, dest_size, src, _TRUNCATE);
#else
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
#endif
}

} // namespace

static ConfigManager* g_cfg_mgr = nullptr;


static long long file_mtime_ms(const fs::path& p)
{
    std::error_code ec;
    auto tp = fs::last_write_time(p, ec);
    if (ec)
        return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
}

ConfigManager::ConfigManager(WindowRegistry& registry)
    : registry_(registry)
{
    config_path_ = "config.toml";
    last_mtime_ = file_mtime_ms(config_path_);
    global_state_.applyDefaults();
}

ConfigManager::~ConfigManager() = default;


bool ConfigManager::saveAll()
{
    last_error_.clear();

    toml::table root = StateSerializer::serializeGlobal(global_state_);

    auto windows = registry_.windowsByType(UIWindowType::Dialog);
    toml::array arr;
    for (auto* w : windows)
    {
        auto* dw = dynamic_cast<DialogWindow*>(w);
        if (!dw)
            continue;
        auto t = StateSerializer::serialize(dw->displayName(), dw->state());
        arr.push_back(std::move(t));
    }
    root.insert("dialogs", std::move(arr));

    auto quest_windows = registry_.windowsByType(UIWindowType::Quest);
    toml::array quests_arr;
    for (auto* w : quest_windows)
    {
        auto* qw = dynamic_cast<QuestWindow*>(w);
        if (!qw)
            continue;
        quests_arr.push_back(StateSerializer::serialize(qw->displayName(), qw->state()));
    }
    if (!quests_arr.empty())
    {
        root.insert("quests", std::move(quests_arr));
    }

    auto quest_helper_windows = registry_.windowsByType(UIWindowType::QuestHelper);
    toml::array quest_helpers_arr;
    for (auto* w : quest_helper_windows)
    {
        auto* qhw = dynamic_cast<QuestHelperWindow*>(w);
        if (!qhw)
            continue;
        quest_helpers_arr.push_back(StateSerializer::serialize(qhw->displayName(), qhw->state()));
    }
    if (!quest_helpers_arr.empty())
    {
        root.insert("quest_helpers", std::move(quest_helpers_arr));
    }

    std::string tmp = config_path_ + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs)
    {
        last_error_ = "Failed to open temp file for writing";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
                                          "Could not create temporary file for writing: " + tmp);
        return false;
    }
    ofs << root;
    ofs.flush();
    ofs.close();
    std::error_code ec;
    fs::rename(tmp, config_path_, ec);
    if (ec)
    {
        last_error_ = std::string("Failed to rename: ") + ec.message();
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
                                          "Could not rename temporary file: " + ec.message());
        return false;
    }
    last_mtime_ = file_mtime_ms(config_path_);
    PLOG_INFO << "Saved config to " << config_path_;
    return true;
}

bool ConfigManager::loadAndApply()
{
    last_error_.clear();
    std::ifstream ifs(config_path_, std::ios::binary);
    if (!ifs)
    {
        return false; // missing file is not an error
    }
    try
    {
        toml::table root = toml::parse(ifs);
        
        // Deserialize global state
        StateSerializer::deserializeGlobal(root, global_state_);
        
        // Apply side effects for settings that need special handling
        global_state_.applyUIScale(global_state_.uiScale());
        
        // Apply profiling level with logger side effects
        {
            int level = global_state_.profilingLevel();
#if DQX_PROFILING_LEVEL >= 1
            if (auto* prof_logger = plog::get<profiling::kProfilingLogInstance>())
            {
                if (level == 0)
                {
                    prof_logger->setMaxSeverity(plog::none);
                }
                else
                {
                    prof_logger->setMaxSeverity(plog::debug);
                }
            }
#endif
        }
        
        // Apply logging level with plog side effects
        {
            int level = global_state_.loggingLevel();
            auto severity = static_cast<plog::Severity>(level);
            
            if (auto* logger = plog::get())
            {
                logger->setMaxSeverity(severity);
            }
            
            if (auto* diag_logger = plog::get<processing::Diagnostics::kLogInstance>())
            {
                diag_logger->setMaxSeverity(severity);
            }
            
            processing::Diagnostics::SetVerbose(level >= 5);
        }

        if (auto* arr = root["dialogs"].as_array())
        {
            std::vector<std::pair<std::string, DialogStateManager>> dialog_configs;
            for (auto&& node : *arr)
            {
                if (!node.is_table())
                    continue;
                auto tbl = *node.as_table();
                DialogStateManager state;
                state.applyDefaults();
                if (state.translation_config().custom_prompt[0] == '\0')
                {
                    safe_strncpy(state.translation_config().custom_prompt.data(),
                                 i18n::get("dialog.settings.default_prompt"),
                                 state.translation_config().custom_prompt.size());
                }
                std::string name;
                if (StateSerializer::deserialize(tbl, state, name))
                {
                    dialog_configs.emplace_back(std::move(name), std::move(state));
                }
                else
                {
                    utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                                                        "Skipped invalid dialog window in configuration",
                                                        "Missing name for dialog entry in config file.");
                }
            }

            if (!dialog_configs.empty())
            {
                auto windows = registry_.windowsByType(UIWindowType::Dialog);
                int have = static_cast<int>(windows.size());
                int want = static_cast<int>(dialog_configs.size());

                if (want > have)
                {
                    for (int i = 0; i < want - have; ++i)
                        registry_.createDialogWindow();
                    windows = registry_.windowsByType(UIWindowType::Dialog);
                }
                else if (want < have)
                {
                    for (int i = have - 1; i >= want; --i)
                    {
                        registry_.removeWindow(windows[i]);
                    }
                    windows = registry_.windowsByType(UIWindowType::Dialog);
                }

                int n = std::min(static_cast<int>(windows.size()), want);
                for (int i = 0; i < n; ++i)
                {
                    auto* dw = dynamic_cast<DialogWindow*>(windows[i]);
                    if (!dw)
                        continue;
                    dw->rename(dialog_configs[i].first.c_str());

                    dw->state() = dialog_configs[i].second;
                    dw->reinitializePlaceholder();

                    // Restore runtime-only state not persisted in config
                    dw->state().ui_state().window_size =
                        ImVec2(dw->state().ui_state().width, dw->state().ui_state().height);
                    dw->state().ui_state().pending_resize = true;
                    dw->state().ui_state().pending_reposition = true;
                    dw->state().ui_state().font = nullptr;
                    dw->state().ui_state().font_base_size = 0.0f;

                    // Rebind font pointers after state replacement
                    dw->refreshFontBinding();
                    dw->initTranslatorIfEnabled();

                    dw->setDefaultInstance(false);
                    if (global_state_.defaultDialogEnabled() && i == 0)
                    {
                        registry_.markDialogAsDefault(*dw);
                    }
                }
            }
        }

        auto applyQuestConfig =
            [&](QuestWindow& quest_window, QuestStateManager& quest_state, const std::string& quest_name)
        {
            if (!quest_name.empty())
                quest_window.rename(quest_name.c_str());

            quest_window.state() = quest_state;
            quest_window.state().quest.applyDefaults();
            quest_window.state().translated.applyDefaults();
            quest_window.state().original.applyDefaults();
            quest_window.state().translation_valid = false;
            quest_window.state().translation_failed = false;
            quest_window.state().translation_error.clear();
            quest_window.state().ui_state().window_size =
                ImVec2(quest_window.state().ui_state().width, quest_window.state().ui_state().height);
            quest_window.state().ui_state().pending_resize = true;
            quest_window.state().ui_state().pending_reposition = true;
            quest_window.state().ui_state().font = nullptr;
            quest_window.state().ui_state().font_base_size = 0.0f;
            quest_window.refreshFontBinding();
            quest_window.initTranslatorIfEnabled();
        };

        auto processQuestTable = [&](const toml::table& quest_tbl, std::size_t index)
        {
            QuestStateManager quest_state;
            quest_state.applyDefaults();
            if (quest_state.translation_config().custom_prompt[0] == '\0')
            {
                safe_strncpy(quest_state.translation_config().custom_prompt.data(),
                             i18n::get("dialog.settings.default_prompt"),
                             quest_state.translation_config().custom_prompt.size());
            }
            std::string quest_name;
            if (!StateSerializer::deserialize(quest_tbl, quest_state, quest_name))
            {
                utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                                                    "Skipped invalid quest window in configuration",
                                                    "Missing name for quest entry in config file.");
                return;
            }

            auto quests = registry_.windowsByType(UIWindowType::Quest);
            QuestWindow* quest_window = nullptr;
            if (index < quests.size())
            {
                quest_window = dynamic_cast<QuestWindow*>(quests[index]);
            }
            if (!quest_window)
            {
                quest_window = &registry_.createQuestWindow();
            }

            if (quest_window)
            {
                applyQuestConfig(*quest_window, quest_state, quest_name);
                quest_window->setDefaultInstance(false);
                if (global_state_.defaultQuestEnabled() && index == 0)
                {
                    registry_.markQuestAsDefault(*quest_window);
                }
            }
        };

        if (auto* quest_arr = root["quests"].as_array())
        {
            std::size_t index = 0;
            for (auto&& node : *quest_arr)
            {
                if (auto* quest_tbl = node.as_table())
                {
                    processQuestTable(*quest_tbl, index);
                    ++index;
                }
            }
        }
        else if (auto* quest_tbl = root["quest"].as_table())
        {
            processQuestTable(*quest_tbl, 0);
        }
        else
        {
            if (global_state_.defaultQuestEnabled() && registry_.windowsByType(UIWindowType::Quest).empty())
            {
                registry_.createQuestWindow(true);
            }
        }

        auto processQuestHelperTable = [&](const toml::table& tbl, std::size_t index) {
            QuestHelperWindow* qh_window = nullptr;
            auto existing_quest_helpers = registry_.windowsByType(UIWindowType::QuestHelper);
            if (index < existing_quest_helpers.size())
            {
                qh_window = dynamic_cast<QuestHelperWindow*>(existing_quest_helpers[index]);
            }
            if (!qh_window)
            {
                qh_window = &registry_.createQuestHelperWindow(false);
            }
            std::string name;
            QuestHelperStateManager quest_helper_state;
            if (StateSerializer::deserialize(tbl, quest_helper_state, name))
            {
                qh_window->rename(name.c_str());
                WindowStateApplier::apply(*qh_window, quest_helper_state);
                qh_window->setDefaultInstance(false);
                if (global_state_.defaultQuestHelperEnabled() && index == 0)
                {
                    registry_.markQuestHelperAsDefault(*qh_window);
                }
            }
        };

        if (auto* quest_helper_arr = root["quest_helpers"].as_array())
        {
            std::size_t index = 0;
            for (auto&& node : *quest_helper_arr)
            {
                if (auto* qh_tbl = node.as_table())
                {
                    processQuestHelperTable(*qh_tbl, index);
                    ++index;
                }
            }
        }
        else
        {
            if (global_state_.defaultQuestHelperEnabled() && registry_.windowsByType(UIWindowType::QuestHelper).empty())
            {
                registry_.createQuestHelperWindow(true);
            }
        }

        return true;
    }
    catch (const toml::parse_error& pe)
    {
        last_error_ = std::string("config parse error: ") + std::string(pe.description());
        PLOG_WARNING << last_error_;

        // Extract line number and error details if available
        std::string error_details = "TOML parse error";
        if (pe.source().begin.line > 0)
        {
            error_details =
                "Error at line " + std::to_string(pe.source().begin.line) + ": " + std::string(pe.description());
        }
        else
        {
            error_details = std::string(pe.description());
        }

        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                                            "Configuration file has errors. Using defaults for invalid entries.",
                                            error_details + "\nFile: " + config_path_);
        return false;
    }
}

bool ConfigManager::loadAtStartup() { return loadAndApply(); }

void ConfigManager::pollAndApply()
{
    fs::path p(config_path_);
    auto mtime = file_mtime_ms(p);
    if (mtime == 0 || mtime == last_mtime_)
        return;

    if (loadAndApply())
    {
        last_mtime_ = mtime;
        last_error_.clear();
        PLOG_INFO << "Config reloaded from " << config_path_;
    }
    else
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration, "Failed to reload configuration",
                                            last_error_.empty() ? std::string("See logs for details") : last_error_);
    }
}

ConfigManager* ConfigManager_Get() { return g_cfg_mgr; }

void ConfigManager_Set(ConfigManager* mgr) { g_cfg_mgr = mgr; }

bool ConfigManager_SaveAll()
{
    if (auto* cm = ConfigManager_Get())
    {
        return cm->saveAll();
    }
    return false;
}
