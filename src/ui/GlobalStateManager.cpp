#include "GlobalStateManager.hpp"
#include "../config/ConfigManager.hpp"
#include "../config/StateSerializer.hpp"

#include <algorithm>
#include <imgui.h>
#include <toml++/toml.h>

GlobalStateManager::GlobalStateManager()
{
    applyDefaults();
}

void GlobalStateManager::applyDefaults()
{
    ui_scale_ = 1.0f;
    append_logs_ = false;
    borderless_windows_ = false;
    app_mode_ = AppMode::Normal;
    window_always_on_top_ = false;
    ui_language_ = "en";

    profiling_level_ = 0;
    logging_level_ = 4;
    verbose_ = false;
    compatibility_mode_ = false;
    hook_wait_timeout_ms_ = 200;

    default_dialog_enabled_ = true;
    default_quest_enabled_ = true;
    default_quest_helper_enabled_ = false;

    translation_config_.applyDefaults();
    translation_version_ = 1;
}

void GlobalStateManager::setUIScale(float scale)
{
    ui_scale_ = std::clamp(scale, 0.1f, 3.0f);
}

void GlobalStateManager::applyUIScale(float scale)
{
    setUIScale(scale);

    if (!style_backup_.valid)
    {
        style_backup_.style = ImGui::GetStyle();
        style_backup_.valid = true;
    }

    ImGui::GetStyle() = style_backup_.style;
    ImGui::GetStyle().ScaleAllSizes(ui_scale_);
    ImGui::GetIO().FontGlobalScale = ui_scale_;
}

void GlobalStateManager::incrementTranslationVersion()
{
    ++translation_version_;
    if (translation_version_ == 0)
    {
        ++translation_version_;
    }
}

void GlobalStateManager::registerConfigHandler(ConfigManager& config)
{
    TableCallbacks cb;
    cb.load = [this](const toml::table& section) {
        StateSerializer::deserializeGlobal(section, *this);
    };
    cb.save = [this]() -> toml::table {
        return StateSerializer::serializeGlobal(*this);
    };
    
    config.registerTable("", std::move(cb), {"global", "app"});
}
