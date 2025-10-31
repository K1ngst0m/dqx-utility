#include "WindowRegistry.hpp"

#include "dialog/DialogWindow.hpp"
#include "quest/QuestWindow.hpp"
#include "quest/QuestHelperWindow.hpp"
#include "help/HelpWindow.hpp"
#include "UIHelper.hpp"
#include "FontManager.hpp"
#include "ui/Localization.hpp"

#include <imgui.h>
#include <algorithm>

// Prepares a registry capable of creating window instances.
WindowRegistry::WindowRegistry(FontManager& font_manager)
    : font_manager_(font_manager)
{
}

// Registers and returns a new dialog window instance.
DialogWindow& WindowRegistry::createDialogWindow(bool mark_default)
{
    auto dialog = std::make_unique<DialogWindow>(font_manager_, dialog_counter_, makeDialogName(), mark_default);
    DialogWindow& ref = *dialog;
    windows_.push_back(std::move(dialog));
    ++dialog_counter_;
    if (mark_default)
        markDialogAsDefault(ref);
    return ref;
}

QuestWindow& WindowRegistry::createQuestWindow(bool mark_default)
{
    auto quest = std::make_unique<QuestWindow>(font_manager_, makeQuestName(), mark_default);
    QuestWindow& ref = *quest;
    windows_.push_back(std::move(quest));
    ++quest_counter_;
    if (mark_default)
        markQuestAsDefault(ref);
    return ref;
}

HelpWindow& WindowRegistry::createHelpWindow()
{
    auto help = std::make_unique<HelpWindow>(font_manager_, makeHelpName());
    HelpWindow& ref = *help;
    windows_.push_back(std::move(help));
    ++help_counter_;
    return ref;
}

QuestHelperWindow& WindowRegistry::createQuestHelperWindow(bool mark_default)
{
    auto quest_helper = std::make_unique<QuestHelperWindow>(font_manager_, makeQuestHelperName());
    QuestHelperWindow& ref = *quest_helper;
    windows_.push_back(std::move(quest_helper));
    ++quest_helper_counter_;
    if (mark_default)
        markQuestHelperAsDefault(ref);
    return ref;
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
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [&](const std::unique_ptr<UIWindow>& entry)
                                  {
                                      return entry.get() == window;
                                  }),
                   windows_.end());
}

// Produces a filtered view for the requested window type.
std::vector<UIWindow*> WindowRegistry::windowsByType(UIWindowType type)
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

// Process removal requests from dialog windows
void WindowRegistry::processRemovals()
{
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [this](const std::unique_ptr<UIWindow>& window)
                                  {
                                      if (window->type() == UIWindowType::Dialog)
                                      {
                                          if (auto* dialog = dynamic_cast<DialogWindow*>(window.get()))
                                          {
                                              if (dialog->shouldBeRemoved())
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
                                              if (quest->shouldBeRemoved())
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
                                      else if (window->type() == UIWindowType::Help)
                                      {
                                          return false;
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
