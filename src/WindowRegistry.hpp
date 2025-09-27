#pragma once

#include <memory>
#include <vector>

enum class UIWindowType
{
    Dialog = 0
};

struct ImGuiIO;
class DialogWindow;
class FontManager;

// UIWindow defines the minimal interface for renderable ImGui windows.
class UIWindow
{
public:
    virtual ~UIWindow() = default;
    virtual UIWindowType type() const = 0;
    virtual const char* label() const = 0;
    virtual void render(ImGuiIO& io) = 0;
    virtual void renderSettings(ImGuiIO& io) = 0;
};

// WindowRegistry tracks all UI windows and their creation helpers.
class WindowRegistry
{
public:
    WindowRegistry(FontManager& font_manager, ImGuiIO& io);

    DialogWindow& createDialogWindow();

    std::vector<std::unique_ptr<UIWindow>>& windows() { return windows_; }
    std::vector<UIWindow*> windowsByType(UIWindowType type);

private:
    FontManager& font_manager_;
    ImGuiIO& io_;
    std::vector<std::unique_ptr<UIWindow>> windows_;
    int dialog_counter_ = 1;
};
