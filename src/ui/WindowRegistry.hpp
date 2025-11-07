#pragma once

#include <memory>
#include <string>
#include <vector>

enum class UIWindowType
{
    Dialog = 0,
    Quest = 1,
    Help = 2,
    QuestHelper = 3
};

class DialogWindow;
class QuestWindow;
class HelpWindow;
class QuestHelperWindow;
class FontManager;
class ConfigManager;
class GlobalStateManager;
class QuestManager;
class MonsterManager;
struct DialogStateManager;
struct QuestStateManager;
struct QuestHelperStateManager;

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

class WindowRegistry
{
public:
    WindowRegistry(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config, QuestManager& quest_manager, MonsterManager& monster_manager);
    ~WindowRegistry();

    DialogWindow& createDialogWindow(bool mark_default = false);
    QuestWindow& createQuestWindow(bool mark_default = false);
    HelpWindow& createHelpWindow();
    QuestHelperWindow& createQuestHelperWindow(bool mark_default = false);
    void removeWindow(UIWindow* window);
    void processRemovals(); // Remove windows marked for removal

    std::vector<std::unique_ptr<UIWindow>>& windows() { return windows_; }

    std::vector<UIWindow*> windowsByType(UIWindowType type) const;

    DialogWindow* defaultDialogWindow() const { return default_dialog_; }

    QuestWindow* defaultQuestWindow() const { return default_quest_; }

    QuestHelperWindow* defaultQuestHelperWindow() const { return default_quest_helper_; }

    void markDialogAsDefault(DialogWindow& window);
    void markQuestAsDefault(QuestWindow& window);
    void markQuestHelperAsDefault(QuestHelperWindow& window);

    void setDefaultDialogEnabled(bool enabled);
    void setDefaultQuestEnabled(bool enabled);
    void setDefaultQuestHelperEnabled(bool enabled);
    void syncDefaultWindows(const GlobalStateManager& state);
    
    void registerWindowStateHandlers();

private:
    std::string makeDialogName();
    std::string makeQuestName();
    std::string makeHelpName();
    std::string makeQuestHelperName();

    FontManager& font_manager_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
    QuestManager& quest_manager_;
    MonsterManager& monster_manager_;
    std::vector<std::unique_ptr<UIWindow>> windows_;
    int dialog_counter_ = 0;
    int quest_counter_ = 0;
    int help_counter_ = 0;
    int quest_helper_counter_ = 0;
    DialogWindow* default_dialog_ = nullptr;
    QuestWindow* default_quest_ = nullptr;
    QuestHelperWindow* default_quest_helper_ = nullptr;

    std::unique_ptr<DialogStateManager> saved_dialog_state_;
    std::unique_ptr<QuestStateManager> saved_quest_state_;
    std::unique_ptr<QuestHelperStateManager> saved_quest_helper_state_;
    std::string saved_dialog_name_;
    std::string saved_quest_name_;
    std::string saved_quest_helper_name_;
};
