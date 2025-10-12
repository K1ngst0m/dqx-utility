#pragma once

#include <imgui.h>

class UITheme
{
public:
    static const ImVec4& dialogBgColor() { return dialog_bg_; }
    static const ImVec4& dialogBorderColor() { return dialog_border_; }
    static const ImVec4& dialogTextColor() { return dialog_text_; }
    static const ImVec4& dialogSeparatorColor() { return dialog_separator_; }
    static const ImVec4& warningColor() { return warning_; }
    static const ImVec4& successColor() { return success_; }
    static const ImVec4& errorColor() { return error_; }
    static const ImVec4& disabledColor() { return disabled_; }
    static const ImVec4& cautionColor() { return caution_; }

    static float dialogSeparatorThickness() { return 3.0f; }
    static float dialogSeparatorSpacing() { return 6.0f; }

    static void pushSettingsWindowStyle();
    static void popSettingsWindowStyle();

    static void pushDialogStyle(float background_alpha, const ImVec2& padding, float rounding, float border_thickness, bool border_enabled = true);
    static void popDialogStyle();

    static ImVec4 statusColor(bool is_success, bool is_error = false, bool is_disabled = false);

    static void applyDockingTheme();

private:
    static constexpr ImVec4 dialog_bg_        = ImVec4(0.0f, 0.0f, 0.0f, 0.78f);
    static constexpr ImVec4 dialog_border_    = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    static constexpr ImVec4 dialog_text_      = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    static constexpr ImVec4 dialog_separator_ = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    static constexpr ImVec4 warning_          = ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
    static constexpr ImVec4 success_          = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    static constexpr ImVec4 error_            = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    static constexpr ImVec4 disabled_         = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    static constexpr ImVec4 caution_          = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
};