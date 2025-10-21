#pragma once

#include <memory>
#include <string>
#include <vector>

enum class UIWindowType
{
    Dialog = 0,
    Quest = 1,
    Help = 2
};

class DialogWindow;
class QuestWindow;
class HelpWindow;
class FontManager;

// UIWindow defines the minimal interface for renderable ImGui windows.
class UIWindow
{
public:
    virtual ~UIWindow() = default;
    virtual UIWindowType type() const = 0;
    virtual const char* displayName() const = 0;
    virtual const char* windowLabel() const = 0;
    virtual void rename(const char* new_name) = 0;
    virtual void render() = 0;
    virtual void renderSettings() = 0;
};

// WindowRegistry tracks all UI windows and their creation helpers.
class WindowRegistry
{
public:
    WindowRegistry(FontManager& font_manager);

    DialogWindow& createDialogWindow(bool mark_default = false);
    QuestWindow& createQuestWindow(bool mark_default = false);
    HelpWindow& createHelpWindow();
    void removeWindow(UIWindow* window);
    void processRemovals(); // Remove windows marked for removal

    std::vector<std::unique_ptr<UIWindow>>& windows() { return windows_; }

    std::vector<UIWindow*> windowsByType(UIWindowType type);

    DialogWindow* defaultDialogWindow() const { return default_dialog_; }

    QuestWindow* defaultQuestWindow() const { return default_quest_; }

    void markDialogAsDefault(DialogWindow& window);
    void markQuestAsDefault(QuestWindow& window);

private:
    std::string makeDialogName();
    std::string makeQuestName();
    std::string makeHelpName();

    FontManager& font_manager_;
    std::vector<std::unique_ptr<UIWindow>> windows_;
    int dialog_counter_ = 0;
    int quest_counter_ = 0;
    int help_counter_ = 0;
    DialogWindow* default_dialog_ = nullptr;
    QuestWindow* default_quest_ = nullptr;
};
