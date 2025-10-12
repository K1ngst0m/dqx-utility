#pragma once

#include <memory>
#include <string>
#include <vector>

enum class UIWindowType
{
    Dialog = 0,
    Quest = 1
};

class DialogWindow;
class QuestWindow;
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

    DialogWindow& createDialogWindow();
    QuestWindow& createQuestWindow();
    void removeWindow(UIWindow* window);
    void processRemovals(); // Remove windows marked for removal

    std::vector<std::unique_ptr<UIWindow>>& windows() { return windows_; }
    std::vector<UIWindow*> windowsByType(UIWindowType type);

private:
    std::string makeDialogName();
    std::string makeQuestName();

    FontManager& font_manager_;
    std::vector<std::unique_ptr<UIWindow>> windows_;
    int dialog_counter_ = 0;
    int quest_counter_ = 0;
};
