#pragma once

#include "../WindowRegistry.hpp"
#include "ui/dialog/DialogStateManager.hpp"

#include <imgui.h>

#include <string>

class FontManager;

class HelpWindow : public UIWindow
{
public:
    HelpWindow(FontManager& font_manager, const std::string& name);
    ~HelpWindow() override;

    UIWindowType type() const override { return UIWindowType::Help; }

    const char* displayName() const override { return name_.c_str(); }

    const char* windowLabel() const override { return window_label_.c_str(); }

    void rename(const char* new_name) override;

    void render() override;
    void renderSettings() override;

    DialogStateManager& state() { return state_; }

private:
    enum class StatusKind
    {
        Ok,
        Warning,
        Error
    };

    struct StatusInfo
    {
        StatusKind kind;
        std::string status_text;
        std::string message;
        ImVec4 color;
    };

    void refreshFontBinding();
    StatusInfo evaluateStatus() const;
    void renderStatusMessage(const StatusInfo& info, ImFont* font, float wrap_width);
    void renderHelpTips(const ImVec4& color, float wrap_width);
    void updateFadeState(StatusKind kind, bool hovered, float delta_time);

    static ImVec4 colorFor(StatusKind kind);
    static std::string sanitizeErrorMessage(const std::string& message);

    FontManager& font_manager_;
    DialogStateManager state_{};
    std::string name_;
    std::string window_label_;
    std::string id_suffix_;

    float ok_idle_timer_ = 0.0f;
    float fade_alpha_ = 1.0f;
    bool last_hovered_ = false;
    float countdown_seconds_ = 0.0f;
};
