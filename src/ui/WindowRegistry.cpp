#include "WindowRegistry.hpp"

#include "dialog/DialogWindow.hpp"
#include "quest/QuestWindow.hpp"
#include "quest/QuestHelperWindow.hpp"
#include "help/HelpWindow.hpp"
#include "monster/MonsterWindow.hpp"
#include "UIHelper.hpp"
#include "FontManager.hpp"
#include "ui/Localization.hpp"
#include "GlobalStateManager.hpp"
#include "dialog/DialogStateManager.hpp"
#include "quest/QuestStateManager.hpp"
#include "quest/QuestHelperStateManager.hpp"
#include "../config/ConfigManager.hpp"
#include "../config/StateSerializer.hpp"
#include "../utils/ErrorReporter.hpp"

#include <imgui.h>
#include <algorithm>
#include <toml++/toml.h>

WindowRegistry::WindowRegistry(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config, 
                               QuestManager& quest_manager, MonsterManager& monster_manager,
                               processing::GlossaryManager& glossary_manager)
    : font_manager_(font_manager)
    , global_state_(global_state)
    , config_(config)
    , quest_manager_(quest_manager)
    , monster_manager_(monster_manager)
    , glossary_manager_(glossary_manager)
{
    // Wire monster link handler to open MonsterWindow
    ui::SetMonsterLinkHandler([this](const std::string& monster_id) {
        this->createMonsterWindow(monster_id);
    });
}

WindowRegistry::~WindowRegistry() = default;

DialogWindow& WindowRegistry::createDialogWindow(bool mark_default)
{
    auto dialog = std::make_unique<DialogWindow>(font_manager_, global_state_, config_, monster_manager_, glossary_manager_, dialog_counter_, makeDialogName(), mark_default);
    DialogWindow& ref = *dialog;
    windows_.push_back(std::move(dialog));
    ++dialog_counter_;
    if (mark_default)
        markDialogAsDefault(ref);
    return ref;
}

QuestWindow& WindowRegistry::createQuestWindow(bool mark_default)
{
    auto quest = std::make_unique<QuestWindow>(font_manager_, global_state_, config_, quest_manager_, monster_manager_, quest_counter_, makeQuestName(), mark_default);
    QuestWindow& ref = *quest;
    windows_.push_back(std::move(quest));
    ++quest_counter_;
    if (mark_default)
        markQuestAsDefault(ref);
    return ref;
}

HelpWindow& WindowRegistry::createHelpWindow()
{
    auto help = std::make_unique<HelpWindow>(font_manager_, global_state_, config_, makeHelpName());
    HelpWindow& ref = *help;
    windows_.push_back(std::move(help));
    ++help_counter_;
    return ref;
}

QuestHelperWindow& WindowRegistry::createQuestHelperWindow(bool mark_default)
{
    auto quest_helper = std::make_unique<QuestHelperWindow>(font_manager_, global_state_, config_, quest_manager_, monster_manager_, makeQuestHelperName());
    QuestHelperWindow& ref = *quest_helper;
    windows_.push_back(std::move(quest_helper));
    ++quest_helper_counter_;
    if (mark_default)
        markQuestHelperAsDefault(ref);
    return ref;
}

MonsterWindow& WindowRegistry::createMonsterWindow(const std::string& monster_id)
{
    // Check if a window for this monster already exists
    auto* existing = findMonsterWindow(monster_id);
    if (existing)
    {
        // Focus existing window on next render
        existing->requestFocus();
        return *existing;
    }

    // Create new window
    auto monster_window = std::make_unique<MonsterWindow>(font_manager_, global_state_, config_, monster_manager_, glossary_manager_, monster_id, makeMonsterName());
    MonsterWindow& ref = *monster_window;
    windows_.push_back(std::move(monster_window));
    ++monster_counter_;
    return ref;
}

MonsterWindow* WindowRegistry::findMonsterWindow(const std::string& monster_id)
{
    for (auto& window : windows_)
    {
        if (window->type() == UIWindowType::Monster)
        {
            if (auto* monster_window = dynamic_cast<MonsterWindow*>(window.get()))
            {
                if (monster_window->monsterId() == monster_id)
                    return monster_window;
            }
        }
    }
    return nullptr;
}

// Removes a window from the registry.
void WindowRegistry::removeWindow(UIWindow* window)
{
    if (!window)
        return;
    if (window == default_dialog_)
    {
        if (auto* dialog = dynamic_cast<DialogWindow*>(window))
            dialog->setDefaultInstance(false);
        default_dialog_ = nullptr;
    }
    if (window == default_quest_)
    {
        if (auto* quest = dynamic_cast<QuestWindow*>(window))
            quest->setDefaultInstance(false);
        default_quest_ = nullptr;
    }
    if (window == default_quest_helper_)
    {
        if (auto* quest_helper = dynamic_cast<QuestHelperWindow*>(window))
            quest_helper->setDefaultInstance(false);
        default_quest_helper_ = nullptr;
    }
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [&](const std::unique_ptr<UIWindow>& entry)
                                  {
                                      return entry.get() == window;
                                  }),
                   windows_.end());
}

// Produces a filtered view for the requested window type.
std::vector<UIWindow*> WindowRegistry::windowsByType(UIWindowType type) const
{
    std::vector<UIWindow*> filtered;
    filtered.reserve(windows_.size());
    for (auto& window : windows_)
    {
        if (window && window->type() == type)
            filtered.push_back(window.get());
    }
    return filtered;
}

void WindowRegistry::processRemovals()
{
    // Count windows by type first
    int dialog_count = 0;
    int quest_count = 0;
    int quest_helper_count = 0;
    int monster_count = 0;
    
    for (const auto& window : windows_)
    {
        switch (window->type())
        {
            case UIWindowType::Dialog:
                ++dialog_count;
                break;
            case UIWindowType::Quest:
                ++quest_count;
                break;
            case UIWindowType::QuestHelper:
                ++quest_helper_count;
                break;
            case UIWindowType::Monster:
                ++monster_count;
                break;
            default:
                break;
        }
    }

    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [this, dialog_count, quest_count, quest_helper_count, monster_count](const std::unique_ptr<UIWindow>& window)
                                  {
                                      if (window->type() == UIWindowType::Dialog)
                                      {
                                          if (auto* dialog = dynamic_cast<DialogWindow*>(window.get()))
                                          {
                                              if (dialog->shouldBeRemoved() && dialog_count > 1)
                                              {
                                                  if (window.get() == default_dialog_)
                                                  {
                                                      dialog->setDefaultInstance(false);
                                                      default_dialog_ = nullptr;
                                                  }
                                                  return true;
                                              }
                                          }
                                      }
                                      else if (window->type() == UIWindowType::Quest)
                                      {
                                          if (auto* quest = dynamic_cast<QuestWindow*>(window.get()))
                                          {
                                              if (quest->shouldBeRemoved() && quest_count > 1)
                                              {
                                                  if (window.get() == default_quest_)
                                                  {
                                                      quest->setDefaultInstance(false);
                                                      default_quest_ = nullptr;
                                                  }
                                                  return true;
                                              }
                                          }
                                      }
                                      else if (window->type() == UIWindowType::QuestHelper)
                                      {
                                          if (auto* quest_helper = dynamic_cast<QuestHelperWindow*>(window.get()))
                                          {
                                              if (quest_helper->shouldBeRemoved() && quest_helper_count > 1)
                                              {
                                                  if (window.get() == default_quest_helper_)
                                                  {
                                                      quest_helper->setDefaultInstance(false);
                                                      default_quest_helper_ = nullptr;
                                                  }
                                                  return true;
                                              }
                                          }
                                      }
                                      else if (window->type() == UIWindowType::Monster)
                                      {
                                          if (auto* monster = dynamic_cast<MonsterWindow*>(window.get()))
                                          {
                                              if (monster->shouldBeRemoved())
                                              {
                                                  return true;
                                              }
                                          }
                                      }
                                      return false;
                                  }),
                   windows_.end());
}

void WindowRegistry::markDialogAsDefault(DialogWindow& window)
{
    if (default_dialog_ == &window)
        return;
    if (default_dialog_)
        default_dialog_->setDefaultInstance(false);
    window.setDefaultInstance(true);
    default_dialog_ = &window;
}

void WindowRegistry::markQuestAsDefault(QuestWindow& window)
{
    if (default_quest_ == &window)
        return;
    if (default_quest_)
        default_quest_->setDefaultInstance(false);
    window.setDefaultInstance(true);
    default_quest_ = &window;
}

void WindowRegistry::markQuestHelperAsDefault(QuestHelperWindow& window)
{
    if (default_quest_helper_ == &window)
        return;
    if (default_quest_helper_)
        default_quest_helper_->setDefaultInstance(false);
    window.setDefaultInstance(true);
    default_quest_helper_ = &window;
}

// Generates a sequential dialog name using alphabetic suffixes.
std::string WindowRegistry::makeDialogName()
{
    std::string suffix;
    int value = dialog_counter_;
    do
    {
        int remainder = value % 26;
        suffix.insert(suffix.begin(), static_cast<char>('A' + remainder));
        value = value / 26 - 1;
    } while (value >= 0);

    return std::string(i18n::get("window.default_name_prefix")) + " " + suffix;
}

std::string WindowRegistry::makeQuestName()
{
    if (quest_counter_ == 0)
    {
        return ui::LocalizedOrFallback("window.quest.default_name", "Quest Log");
    }
    return ui::LocalizedOrFallback("window.quest.default_name", "Quest Log") + " " + std::to_string(quest_counter_ + 1);
}

std::string WindowRegistry::makeHelpName()
{
    if (help_counter_ == 0)
    {
        return ui::LocalizedOrFallback("window.help.default_name", "Help");
    }
    return ui::LocalizedOrFallback("window.help.default_name", "Help") + " " + std::to_string(help_counter_ + 1);
}

std::string WindowRegistry::makeQuestHelperName()
{
    if (quest_helper_counter_ == 0)
    {
        return ui::LocalizedOrFallback("window.quest_helper.default_name", "Quest Helper");
    }
    return ui::LocalizedOrFallback("window.quest_helper.default_name", "Quest Helper") + " " + std::to_string(quest_helper_counter_ + 1);
}

std::string WindowRegistry::makeMonsterName()
{
    // Count existing monster windows
    int existing_count = 0;
    for (const auto& window : windows_)
    {
        if (window->type() == UIWindowType::Monster)
            ++existing_count;
    }
    
    if (existing_count == 0)
    {
        return ui::LocalizedOrFallback("window.monster.default_name", "Monster Info");
    }
    return ui::LocalizedOrFallback("window.monster.default_name", "Monster Info") + " " + std::to_string(existing_count + 1);
}

void WindowRegistry::setDefaultDialogEnabled(bool enabled)
{
    if (!enabled && default_dialog_)
    {
        saved_dialog_state_ = std::make_unique<DialogStateManager>(default_dialog_->state());
        saved_dialog_name_ = default_dialog_->displayName();
        removeWindow(default_dialog_);
    }
    else if (enabled && !default_dialog_)
    {
        auto& dialog = createDialogWindow(true);
        if (saved_dialog_state_)
        {
            dialog.state() = *saved_dialog_state_;
            dialog.rename(saved_dialog_name_.c_str());
        }
    }
}

void WindowRegistry::setDefaultQuestEnabled(bool enabled)
{
    if (!enabled && default_quest_)
    {
        saved_quest_state_ = std::make_unique<QuestStateManager>(default_quest_->state());
        saved_quest_name_ = default_quest_->displayName();
        removeWindow(default_quest_);
    }
    else if (enabled && !default_quest_)
    {
        auto& quest = createQuestWindow(true);
        if (saved_quest_state_)
        {
            quest.state() = *saved_quest_state_;
            quest.rename(saved_quest_name_.c_str());
        }
    }
}

void WindowRegistry::setDefaultQuestHelperEnabled(bool enabled)
{
    if (!enabled && default_quest_helper_)
    {
        saved_quest_helper_state_ = std::make_unique<QuestHelperStateManager>(default_quest_helper_->state());
        saved_quest_helper_name_ = default_quest_helper_->displayName();
        removeWindow(default_quest_helper_);
    }
    else if (enabled && !default_quest_helper_)
    {
        auto& quest_helper = createQuestHelperWindow(true);
        if (saved_quest_helper_state_)
        {
            quest_helper.state() = *saved_quest_helper_state_;
            quest_helper.rename(saved_quest_helper_name_.c_str());
        }
    }
}

void WindowRegistry::syncDefaultWindows(const GlobalStateManager& state)
{
    setDefaultDialogEnabled(state.defaultDialogEnabled());
    setDefaultQuestEnabled(state.defaultQuestEnabled());
    setDefaultQuestHelperEnabled(state.defaultQuestHelperEnabled());
}

namespace
{
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
}

void WindowRegistry::registerWindowStateHandlers()
{
    // Dialog windows handler
    {
        TableCallbacks cb;
        cb.load = [this](const toml::table& section) {
            auto* arr = section["dialogs"].as_array();
            if (!arr)
                return;
            
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
                auto windows = windowsByType(UIWindowType::Dialog);
                int have = static_cast<int>(windows.size());
                int want = static_cast<int>(dialog_configs.size());

                if (want > have)
                {
                    for (int i = 0; i < want - have; ++i)
                        createDialogWindow();
                    windows = windowsByType(UIWindowType::Dialog);
                }
                else if (want < have)
                {
                    for (int i = have - 1; i >= want; --i)
                    {
                        removeWindow(windows[i]);
                    }
                    windows = windowsByType(UIWindowType::Dialog);
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

                    dw->state().ui_state().window_size =
                        ImVec2(dw->state().ui_state().width, dw->state().ui_state().height);
                    dw->state().ui_state().pending_resize = true;
                    dw->state().ui_state().pending_reposition = true;
                    dw->state().ui_state().font = nullptr;
                    dw->state().ui_state().font_base_size = 0.0f;

                    dw->refreshFontBinding();
                    dw->initTranslatorIfEnabled();

                    dw->setDefaultInstance(false);
                    if (global_state_.defaultDialogEnabled() && i == 0)
                    {
                        markDialogAsDefault(*dw);
                    }
                }
            }
        };
        
        cb.save = [this]() -> toml::table {
            toml::table t;
            auto windows = windowsByType(UIWindowType::Dialog);
            toml::array arr;
            for (auto* w : windows)
            {
                auto* dw = dynamic_cast<DialogWindow*>(w);
                if (!dw)
                    continue;
                auto tbl = StateSerializer::serialize(dw->displayName(), dw->state());
                arr.push_back(std::move(tbl));
            }
            t.insert("dialogs", std::move(arr));
            return t;
        };
        
        config_.registerTable("", std::move(cb), {"dialogs"});
    }
    
    // Quest windows handler
    {
        TableCallbacks cb;
        cb.load = [this](const toml::table& section) {
            auto* arr = section["quests"].as_array();
            if (!arr)
                return;
            
            std::vector<std::pair<std::string, QuestStateManager>> quest_configs;
            for (auto&& node : *arr)
            {
                if (!node.is_table())
                    continue;
                auto tbl = *node.as_table();
                QuestStateManager state;
                state.applyDefaults();
                std::string name;
                if (StateSerializer::deserialize(tbl, state, name))
                {
                    quest_configs.emplace_back(std::move(name), std::move(state));
                }
            }

            if (!quest_configs.empty())
            {
                auto windows = windowsByType(UIWindowType::Quest);
                int have = static_cast<int>(windows.size());
                int want = static_cast<int>(quest_configs.size());

                if (want > have)
                {
                    for (int i = 0; i < want - have; ++i)
                        createQuestWindow();
                    windows = windowsByType(UIWindowType::Quest);
                }
                else if (want < have)
                {
                    for (int i = have - 1; i >= want; --i)
                    {
                        removeWindow(windows[i]);
                    }
                    windows = windowsByType(UIWindowType::Quest);
                }

                int n = std::min(static_cast<int>(windows.size()), want);
                for (int i = 0; i < n; ++i)
                {
                    auto* qw = dynamic_cast<QuestWindow*>(windows[i]);
                    if (!qw)
                        continue;
                    
                    if (!quest_configs[i].first.empty())
                        qw->rename(quest_configs[i].first.c_str());

                    qw->state() = quest_configs[i].second;
                    qw->state().quest.applyDefaults();
                    qw->state().translated.applyDefaults();
                    qw->state().original.applyDefaults();
                    qw->state().translation_valid = false;
                    qw->state().translation_failed = false;
                    qw->state().translation_error.clear();
                    qw->state().ui_state().window_size =
                        ImVec2(qw->state().ui_state().width, qw->state().ui_state().height);
                    qw->state().ui_state().pending_resize = true;
                    qw->state().ui_state().pending_reposition = true;
                    qw->state().ui_state().font = nullptr;
                    qw->state().ui_state().font_base_size = 0.0f;
                    qw->refreshFontBinding();
                    qw->initTranslatorIfEnabled();

                    qw->setDefaultInstance(false);
                    if (global_state_.defaultQuestEnabled() && i == 0)
                    {
                        markQuestAsDefault(*qw);
                    }
                }
            }
        };
        
        cb.save = [this]() -> toml::table {
            toml::table t;
            auto windows = windowsByType(UIWindowType::Quest);
            toml::array arr;
            for (auto* w : windows)
            {
                auto* qw = dynamic_cast<QuestWindow*>(w);
                if (!qw)
                    continue;
                arr.push_back(StateSerializer::serialize(qw->displayName(), qw->state()));
            }
            if (!arr.empty())
            {
                t.insert("quests", std::move(arr));
            }
            return t;
        };
        
        config_.registerTable("", std::move(cb), {"quests"});
    }
    
    // Quest helper windows handler
    {
        TableCallbacks cb;
        cb.load = [this](const toml::table& section) {
            auto* arr = section["quest_helpers"].as_array();
            if (!arr)
                return;
            
            std::vector<std::pair<std::string, QuestHelperStateManager>> helper_configs;
            for (auto&& node : *arr)
            {
                if (!node.is_table())
                    continue;
                auto tbl = *node.as_table();
                QuestHelperStateManager state;
                state.applyDefaults();
                std::string name;
                if (StateSerializer::deserialize(tbl, state, name))
                {
                    helper_configs.emplace_back(std::move(name), std::move(state));
                }
            }

            if (!helper_configs.empty())
            {
                auto windows = windowsByType(UIWindowType::QuestHelper);
                int have = static_cast<int>(windows.size());
                int want = static_cast<int>(helper_configs.size());

                if (want > have)
                {
                    for (int i = 0; i < want - have; ++i)
                        createQuestHelperWindow();
                    windows = windowsByType(UIWindowType::QuestHelper);
                }
                else if (want < have)
                {
                    for (int i = have - 1; i >= want; --i)
                    {
                        removeWindow(windows[i]);
                    }
                    windows = windowsByType(UIWindowType::QuestHelper);
                }

                int n = std::min(static_cast<int>(windows.size()), want);
                for (int i = 0; i < n; ++i)
                {
                    auto* qhw = dynamic_cast<QuestHelperWindow*>(windows[i]);
                    if (!qhw)
                        continue;
                    
                    if (!helper_configs[i].first.empty())
                        qhw->rename(helper_configs[i].first.c_str());

                    qhw->state() = helper_configs[i].second;
                    qhw->state().ui_state().window_size =
                        ImVec2(qhw->state().ui_state().width, qhw->state().ui_state().height);
                    qhw->state().ui_state().pending_resize = true;
                    qhw->state().ui_state().pending_reposition = true;
                    qhw->state().ui_state().font = nullptr;
                    qhw->state().ui_state().font_base_size = 0.0f;
                    qhw->refreshFontBinding();

                    qhw->setDefaultInstance(false);
                    if (global_state_.defaultQuestHelperEnabled() && i == 0)
                    {
                        markQuestHelperAsDefault(*qhw);
                    }
                }
            }
        };
        
        cb.save = [this]() -> toml::table {
            toml::table t;
            auto windows = windowsByType(UIWindowType::QuestHelper);
            toml::array arr;
            for (auto* w : windows)
            {
                auto* qhw = dynamic_cast<QuestHelperWindow*>(w);
                if (!qhw)
                    continue;
                arr.push_back(StateSerializer::serialize(qhw->displayName(), qhw->state()));
            }
            if (!arr.empty())
            {
                t.insert("quest_helpers", std::move(arr));
            }
            return t;
        };
        
        config_.registerTable("", std::move(cb), {"quest_helpers"});
    }
}
