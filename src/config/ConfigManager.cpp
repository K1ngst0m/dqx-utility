#include "ConfigManager.hpp"

#include "../ui/WindowRegistry.hpp"
#include "../ui/dialog/DialogWindow.hpp"
#include "../ui/quest/QuestWindow.hpp"
#include "../state/DialogStateManager.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../processing/Diagnostics.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <imgui.h>

namespace fs = std::filesystem;

static ConfigManager* g_cfg_mgr = nullptr;

static toml::table dialogStateToToml(const std::string& name, const DialogStateManager& state)
{
    toml::table t;
    t.insert("name", name);
    toml::table behavior;
    behavior.insert("auto_scroll_to_new", state.ui_state().auto_scroll_to_new);
    t.insert("behavior", std::move(behavior));

    toml::table translation;
    translation.insert("use_global_translation", state.use_global_translation);
    translation.insert("translate_enabled", state.translation_config().translate_enabled);
    translation.insert("auto_apply_changes", state.translation_config().auto_apply_changes);
    translation.insert("translation_backend", static_cast<int>(state.translation_config().translation_backend));

    std::string target_lang;
    switch (state.translation_config().target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US: target_lang = "en-us"; break;
    case TranslationConfig::TargetLang::ZH_CN: target_lang = "zh-cn"; break;
    case TranslationConfig::TargetLang::ZH_TW: target_lang = "zh-tw"; break;
    }
    translation.insert("target_lang", target_lang);

    toml::table openai;
    openai.insert("api_key", std::string(state.translation_config().openai_api_key.data()));
    openai.insert("base_url", std::string(state.translation_config().openai_base_url.data()));
    openai.insert("model", std::string(state.translation_config().openai_model.data()));
    translation.insert("openai", std::move(openai));

    toml::table google;
    google.insert("api_key", std::string(state.translation_config().google_api_key.data()));
    translation.insert("google", std::move(google));

    toml::table qwen;
    qwen.insert("api_key", std::string(state.translation_config().qwen_api_key.data()));
    qwen.insert("model", std::string(state.translation_config().qwen_model.data()));
    translation.insert("qwen", std::move(qwen));

    toml::table niutrans;
    niutrans.insert("api_key", std::string(state.translation_config().niutrans_api_key.data()));
    translation.insert("niutrans", std::move(niutrans));

    toml::table zhipu;
    zhipu.insert("api_key", std::string(state.translation_config().zhipu_api_key.data()));
    zhipu.insert("base_url", std::string(state.translation_config().zhipu_base_url.data()));
    zhipu.insert("model", std::string(state.translation_config().zhipu_model.data()));
    translation.insert("zhipu", std::move(zhipu));

    toml::table youdao;
    youdao.insert("app_key", std::string(state.translation_config().youdao_app_key.data()));
    youdao.insert("app_secret", std::string(state.translation_config().youdao_app_secret.data()));
    youdao.insert("mode", static_cast<int>(state.translation_config().youdao_mode));
    translation.insert("youdao", std::move(youdao));

    t.insert("translation", std::move(translation));

    toml::table appearance;
    appearance.insert("width", state.ui_state().width);
    appearance.insert("height", state.ui_state().height);
    appearance.insert("pos_x", state.ui_state().window_pos.x);
    appearance.insert("pos_y", state.ui_state().window_pos.y);
    appearance.insert("padding_x", state.ui_state().padding.x);
    appearance.insert("padding_y", state.ui_state().padding.y);
    appearance.insert("rounding", state.ui_state().rounding);
    appearance.insert("border_thickness", state.ui_state().border_thickness);
    appearance.insert("background_alpha", state.ui_state().background_alpha);
    appearance.insert("font_size", state.ui_state().font_size);
    appearance.insert("vignette_thickness", state.ui_state().vignette_thickness);
    appearance.insert("font_path", std::string(state.ui_state().font_path.data()));
    appearance.insert("fade_enabled", state.ui_state().fade_enabled);
    appearance.insert("fade_timeout", state.ui_state().fade_timeout);
    t.insert("appearance", std::move(appearance));

    return t;
}

static bool tomlToDialogState(const toml::table& t, DialogStateManager& state, std::string& name)
{
    auto name_val = t["name"].value<std::string>();
    if (!name_val) return false;
    name = *name_val;

    if (auto* behavior = t["behavior"].as_table())
    {
        if (auto v = (*behavior)["auto_scroll_to_new"].value<bool>())
            state.ui_state().auto_scroll_to_new = *v;
    }

    if (auto* translation_tbl = t["translation"].as_table())
    {
        if (auto v = (*translation_tbl)["use_global_translation"].value<bool>())
            state.use_global_translation = *v;
        if (auto v = (*translation_tbl)["translate_enabled"].value<bool>())
            state.translation_config().translate_enabled = *v;
        if (auto v = (*translation_tbl)["auto_apply_changes"].value<bool>())
            state.translation_config().auto_apply_changes = *v;
        if (auto v = (*translation_tbl)["translation_backend"].value<int>())
            state.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(*v);
        if (auto v = (*translation_tbl)["target_lang"].value<std::string>())
        {
            if (*v == "en-us") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::EN_US;
            else if (*v == "zh-cn") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
            else if (*v == "zh-tw") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
        }

        if (auto* openai_tbl = (*translation_tbl)["openai"].as_table())
        {
            if (auto v = (*openai_tbl)["base_url"].value<std::string>())
                std::snprintf(state.translation_config().openai_base_url.data(), state.translation_config().openai_base_url.size(), "%s", v->c_str());
            if (auto v = (*openai_tbl)["model"].value<std::string>())
                std::snprintf(state.translation_config().openai_model.data(), state.translation_config().openai_model.size(), "%s", v->c_str());
            if (auto v = (*openai_tbl)["api_key"].value<std::string>())
                std::snprintf(state.translation_config().openai_api_key.data(), state.translation_config().openai_api_key.size(), "%s", v->c_str());
        }

        if (auto* google_tbl = (*translation_tbl)["google"].as_table())
        {
            if (auto v = (*google_tbl)["api_key"].value<std::string>())
                std::snprintf(state.translation_config().google_api_key.data(), state.translation_config().google_api_key.size(), "%s", v->c_str());
        }

        if (auto* qwen_tbl = (*translation_tbl)["qwen"].as_table())
        {
            if (auto v = (*qwen_tbl)["api_key"].value<std::string>())
                std::snprintf(state.translation_config().qwen_api_key.data(), state.translation_config().qwen_api_key.size(), "%s", v->c_str());
            if (auto v = (*qwen_tbl)["model"].value<std::string>())
                std::snprintf(state.translation_config().qwen_model.data(), state.translation_config().qwen_model.size(), "%s", v->c_str());
        }

        if (auto* niutrans_tbl = (*translation_tbl)["niutrans"].as_table())
        {
            if (auto v = (*niutrans_tbl)["api_key"].value<std::string>())
                std::snprintf(state.translation_config().niutrans_api_key.data(), state.translation_config().niutrans_api_key.size(), "%s", v->c_str());
        }

        if (auto* zhipu_tbl = (*translation_tbl)["zhipu"].as_table())
        {
            if (auto v = (*zhipu_tbl)["base_url"].value<std::string>())
                std::snprintf(state.translation_config().zhipu_base_url.data(), state.translation_config().zhipu_base_url.size(), "%s", v->c_str());
            if (auto v = (*zhipu_tbl)["model"].value<std::string>())
                std::snprintf(state.translation_config().zhipu_model.data(), state.translation_config().zhipu_model.size(), "%s", v->c_str());
            if (auto v = (*zhipu_tbl)["api_key"].value<std::string>())
                std::snprintf(state.translation_config().zhipu_api_key.data(), state.translation_config().zhipu_api_key.size(), "%s", v->c_str());
        }

        if (auto* youdao_tbl = (*translation_tbl)["youdao"].as_table())
        {
            if (auto v = (*youdao_tbl)["app_key"].value<std::string>())
                std::snprintf(state.translation_config().youdao_app_key.data(), state.translation_config().youdao_app_key.size(), "%s", v->c_str());
            if (auto v = (*youdao_tbl)["app_secret"].value<std::string>())
                std::snprintf(state.translation_config().youdao_app_secret.data(), state.translation_config().youdao_app_secret.size(), "%s", v->c_str());
            if (auto v = (*youdao_tbl)["mode"].value<int>())
            {
                if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                    state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                else
                    state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::Text;
            }
        }
    }

    if (auto* appearance = t["appearance"].as_table())
    {
        if (auto v = (*appearance)["width"].value<double>()) state.ui_state().width = static_cast<float>(*v);
        if (auto v = (*appearance)["height"].value<double>()) state.ui_state().height = static_cast<float>(*v);
        if (auto v = (*appearance)["pos_x"].value<double>()) state.ui_state().window_pos.x = static_cast<float>(*v);
        if (auto v = (*appearance)["pos_y"].value<double>()) state.ui_state().window_pos.y = static_cast<float>(*v);
        if (auto v = (*appearance)["padding_x"].value<double>()) state.ui_state().padding.x = static_cast<float>(*v);
        if (auto v = (*appearance)["padding_y"].value<double>()) state.ui_state().padding.y = static_cast<float>(*v);
        if (auto v = (*appearance)["rounding"].value<double>()) state.ui_state().rounding = static_cast<float>(*v);
        if (auto v = (*appearance)["border_thickness"].value<double>()) state.ui_state().border_thickness = static_cast<float>(*v);
        if (auto v = (*appearance)["background_alpha"].value<double>()) state.ui_state().background_alpha = static_cast<float>(*v);
        if (auto v = (*appearance)["font_size"].value<double>()) state.ui_state().font_size = static_cast<float>(*v);
        if (auto v = (*appearance)["vignette_thickness"].value<double>()) state.ui_state().vignette_thickness = static_cast<float>(*v);
        if (auto v = (*appearance)["font_path"].value<std::string>())
            std::snprintf(state.ui_state().font_path.data(), state.ui_state().font_path.size(), "%s", v->c_str());
        if (auto v = (*appearance)["fade_enabled"].value<bool>()) state.ui_state().fade_enabled = *v;
        if (auto v = (*appearance)["fade_timeout"].value<double>()) state.ui_state().fade_timeout = static_cast<float>(*v);
    }

    if (auto v = t["auto_scroll_to_new"].value<bool>()) state.ui_state().auto_scroll_to_new = *v;
    if (auto v = t["use_global_translation"].value<bool>()) state.use_global_translation = *v;
    if (auto v = t["translate_enabled"].value<bool>()) state.translation_config().translate_enabled = *v;
    if (auto v = t["auto_apply_changes"].value<bool>()) state.translation_config().auto_apply_changes = *v;
    if (auto v = t["translation_backend"].value<int>())
        state.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(*v);

    if (auto v = t["target_lang"].value<std::string>())
    {
        if (*v == "en-us") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::EN_US;
        else if (*v == "zh-cn") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
        else if (*v == "zh-tw") state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
    }

    if (auto v = t["openai_base_url"].value<std::string>())
        std::snprintf(state.translation_config().openai_base_url.data(), state.translation_config().openai_base_url.size(), "%s", v->c_str());
    if (auto v = t["openai_model"].value<std::string>())
        std::snprintf(state.translation_config().openai_model.data(), state.translation_config().openai_model.size(), "%s", v->c_str());
    if (auto v = t["openai_api_key"].value<std::string>())
        std::snprintf(state.translation_config().openai_api_key.data(), state.translation_config().openai_api_key.size(), "%s", v->c_str());
    if (auto v = t["google_api_key"].value<std::string>())
        std::snprintf(state.translation_config().google_api_key.data(), state.translation_config().google_api_key.size(), "%s", v->c_str());
    if (auto v = t["qwen_api_key"].value<std::string>())
        std::snprintf(state.translation_config().qwen_api_key.data(), state.translation_config().qwen_api_key.size(), "%s", v->c_str());
    if (auto v = t["qwen_model"].value<std::string>())
        std::snprintf(state.translation_config().qwen_model.data(), state.translation_config().qwen_model.size(), "%s", v->c_str());
    if (auto v = t["niutrans_api_key"].value<std::string>())
        std::snprintf(state.translation_config().niutrans_api_key.data(), state.translation_config().niutrans_api_key.size(), "%s", v->c_str());
    if (auto v = t["zhipu_base_url"].value<std::string>())
        std::snprintf(state.translation_config().zhipu_base_url.data(), state.translation_config().zhipu_base_url.size(), "%s", v->c_str());
    if (auto v = t["zhipu_model"].value<std::string>())
        std::snprintf(state.translation_config().zhipu_model.data(), state.translation_config().zhipu_model.size(), "%s", v->c_str());
    if (auto v = t["zhipu_api_key"].value<std::string>())
        std::snprintf(state.translation_config().zhipu_api_key.data(), state.translation_config().zhipu_api_key.size(), "%s", v->c_str());
    if (auto v = t["youdao_app_key"].value<std::string>())
        std::snprintf(state.translation_config().youdao_app_key.data(), state.translation_config().youdao_app_key.size(), "%s", v->c_str());
    if (auto v = t["youdao_app_secret"].value<std::string>())
        std::snprintf(state.translation_config().youdao_app_secret.data(), state.translation_config().youdao_app_secret.size(), "%s", v->c_str());
    if (auto v = t["youdao_mode"].value<int>())
    {
        if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
            state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
        else
            state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::Text;
    }

    // GUI properties
    if (auto v = t["width"].value<double>()) state.ui_state().width = static_cast<float>(*v);
    if (auto v = t["height"].value<double>()) state.ui_state().height = static_cast<float>(*v);
    if (auto v = t["pos_x"].value<double>()) state.ui_state().window_pos.x = static_cast<float>(*v);
    if (auto v = t["pos_y"].value<double>()) state.ui_state().window_pos.y = static_cast<float>(*v);
    if (auto v = t["padding_x"].value<double>()) state.ui_state().padding.x = static_cast<float>(*v);
    if (auto v = t["padding_y"].value<double>()) state.ui_state().padding.y = static_cast<float>(*v);
    if (auto v = t["rounding"].value<double>()) state.ui_state().rounding = static_cast<float>(*v);
    if (auto v = t["border_thickness"].value<double>()) state.ui_state().border_thickness = static_cast<float>(*v);
    if (auto v = t["background_alpha"].value<double>()) state.ui_state().background_alpha = static_cast<float>(*v);
    if (auto v = t["font_size"].value<double>()) state.ui_state().font_size = static_cast<float>(*v);
    if (auto v = t["vignette_thickness"].value<double>()) state.ui_state().vignette_thickness = static_cast<float>(*v);
    if (auto v = t["font_path"].value<std::string>())
        std::snprintf(state.ui_state().font_path.data(), state.ui_state().font_path.size(), "%s", v->c_str());
    
    // Fade settings
    if (auto v = t["fade_enabled"].value<bool>()) state.ui_state().fade_enabled = *v;
    if (auto v = t["fade_timeout"].value<double>()) state.ui_state().fade_timeout = static_cast<float>(*v);

    return true;
}


static long long file_mtime_ms(const fs::path& p)
{
    std::error_code ec;
    auto tp = fs::last_write_time(p, ec);
    if (ec) return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
}

ConfigManager::ConfigManager()
{
    config_path_ = "config.toml";
    last_mtime_ = file_mtime_ms(config_path_);
    global_translation_config_.applyDefaults();
    applyVerboseSetting();
}

ConfigManager::~ConfigManager() = default;

void ConfigManager::setUIScale(float scale)
{
    if (scale <= 0.1f) scale = 0.1f;
    if (scale > 3.0f) scale = 3.0f;
    if (!base_.valid)
    {
        base_.style = ImGui::GetStyle();
        base_.valid = true;
    }
    ui_scale_ = scale;
    ImGui::GetStyle() = base_.style;
    ImGui::GetStyle().ScaleAllSizes(ui_scale_);
    ImGui::GetIO().FontGlobalScale = ui_scale_;
}

void ConfigManager::setRegistry(WindowRegistry* reg)
{
    registry_ = reg;
}


bool ConfigManager::saveAll()
{
    last_error_.clear();
    if (!registry_)
    {
        last_error_ = "No registry assigned";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
            "Unable to save configuration",
            "Window registry was not available while saving settings.");
        return false;
    }

    toml::table root;
    toml::table global;
    global.insert("ui_scale", ui_scale_);
    global.insert("append_logs", append_logs_);
    global.insert("borderless_windows", borderless_windows_);
    global.insert("app_mode", static_cast<int>(app_mode_));
    global.insert("window_always_on_top", window_always_on_top_);
    global.insert("ui_language", ui_language_);
    global.insert("dialog_fade_enabled", dialog_fade_enabled_);
    global.insert("dialog_fade_timeout", dialog_fade_timeout_);
    global.insert("verbose_logging", verbose_logging_);

    toml::table translation;
    translation.insert("translate_enabled", global_translation_config_.translate_enabled);
    translation.insert("auto_apply_changes", global_translation_config_.auto_apply_changes);
    translation.insert("translation_backend", static_cast<int>(global_translation_config_.translation_backend));
    std::string global_target_lang;
    switch (global_translation_config_.target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US: global_target_lang = "en-us"; break;
    case TranslationConfig::TargetLang::ZH_CN: global_target_lang = "zh-cn"; break;
    case TranslationConfig::TargetLang::ZH_TW: global_target_lang = "zh-tw"; break;
    }
    translation.insert("target_lang", global_target_lang);

    toml::table openai;
    openai.insert("api_key", std::string(global_translation_config_.openai_api_key.data()));
    openai.insert("base_url", std::string(global_translation_config_.openai_base_url.data()));
    openai.insert("model", std::string(global_translation_config_.openai_model.data()));
    translation.insert("openai", std::move(openai));

    toml::table google;
    google.insert("api_key", std::string(global_translation_config_.google_api_key.data()));
    translation.insert("google", std::move(google));

    toml::table qwen;
    qwen.insert("api_key", std::string(global_translation_config_.qwen_api_key.data()));
    qwen.insert("model", std::string(global_translation_config_.qwen_model.data()));
    translation.insert("qwen", std::move(qwen));

    toml::table niutrans;
    niutrans.insert("api_key", std::string(global_translation_config_.niutrans_api_key.data()));
    translation.insert("niutrans", std::move(niutrans));

    toml::table zhipu;
    zhipu.insert("api_key", std::string(global_translation_config_.zhipu_api_key.data()));
    zhipu.insert("base_url", std::string(global_translation_config_.zhipu_base_url.data()));
    zhipu.insert("model", std::string(global_translation_config_.zhipu_model.data()));
    translation.insert("zhipu", std::move(zhipu));

    toml::table youdao;
    youdao.insert("app_key", std::string(global_translation_config_.youdao_app_key.data()));
    youdao.insert("app_secret", std::string(global_translation_config_.youdao_app_secret.data()));
    youdao.insert("mode", static_cast<int>(global_translation_config_.youdao_mode));
    translation.insert("youdao", std::move(youdao));
    global.insert("translation", std::move(translation));

    root.insert("global", std::move(global));

    auto windows = registry_->windowsByType(UIWindowType::Dialog);
    toml::array arr;
    for (auto* w : windows)
    {
        auto* dw = dynamic_cast<DialogWindow*>(w);
        if (!dw) continue;
        auto t = dialogStateToToml(dw->displayName(), dw->state());
        arr.push_back(std::move(t));
    }
    root.insert("dialogs", std::move(arr));

    auto quest_windows = registry_->windowsByType(UIWindowType::Quest);
    if (!quest_windows.empty())
    {
        toml::array quests_arr;
        for (auto* w : quest_windows)
        {
            auto* qw = dynamic_cast<QuestWindow*>(w);
            if (!qw)
                continue;
            quests_arr.push_back(dialogStateToToml(qw->displayName(), qw->state()));
        }
        if (!quests_arr.empty())
        {
            root.insert("quests", std::move(quests_arr));
        }
    }

    std::string tmp = config_path_ + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs)
    {
        last_error_ = "Failed to open temp file for writing";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
            "Failed to save configuration",
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
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
            "Failed to save configuration",
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
        // global
        if (auto* g = root["global"].as_table())
        {
            if (auto v = (*g)["ui_scale"].value<float>())
                setUIScale(*v);
            if (auto v = (*g)["append_logs"].value<bool>())
                append_logs_ = *v;
            if (auto v = (*g)["borderless_windows"].value<bool>())
                borderless_windows_ = *v;
            if (auto v = (*g)["app_mode"].value<int>())
                app_mode_ = static_cast<AppMode>(*v);
            if (auto v = (*g)["window_always_on_top"].value<bool>())
                window_always_on_top_ = *v;
            if (auto v = (*g)["ui_language"].value<std::string>())
                ui_language_ = *v;
            if (auto v = (*g)["dialog_fade_enabled"].value<bool>())
                dialog_fade_enabled_ = *v;
            if (auto v = (*g)["dialog_fade_timeout"].value<double>())
                dialog_fade_timeout_ = static_cast<float>(*v);
            if (auto v = (*g)["verbose_logging"].value<bool>())
                setVerboseLogging(*v);

            global_translation_config_.applyDefaults();
            if (auto* trans = (*g)["translation"].as_table())
            {
                if (auto v = (*trans)["translate_enabled"].value<bool>()) global_translation_config_.translate_enabled = *v;
                if (auto v = (*trans)["auto_apply_changes"].value<bool>()) global_translation_config_.auto_apply_changes = *v;
                if (auto v = (*trans)["translation_backend"].value<int>())
                    global_translation_config_.translation_backend = static_cast<TranslationConfig::TranslationBackend>(*v);
                if (auto v = (*trans)["target_lang"].value<std::string>())
                {
                    if (*v == "en-us") global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::EN_US;
                    else if (*v == "zh-cn") global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
                    else if (*v == "zh-tw") global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
                }
                if (auto* openai_tbl = (*trans)["openai"].as_table())
                {
                    if (auto v = (*openai_tbl)["base_url"].value<std::string>())
                        std::snprintf(global_translation_config_.openai_base_url.data(), global_translation_config_.openai_base_url.size(), "%s", v->c_str());
                    if (auto v = (*openai_tbl)["model"].value<std::string>())
                        std::snprintf(global_translation_config_.openai_model.data(), global_translation_config_.openai_model.size(), "%s", v->c_str());
                    if (auto v = (*openai_tbl)["api_key"].value<std::string>())
                        std::snprintf(global_translation_config_.openai_api_key.data(), global_translation_config_.openai_api_key.size(), "%s", v->c_str());
                }
                if (auto* google_tbl = (*trans)["google"].as_table())
                {
                    if (auto v = (*google_tbl)["api_key"].value<std::string>())
                        std::snprintf(global_translation_config_.google_api_key.data(), global_translation_config_.google_api_key.size(), "%s", v->c_str());
                }
                if (auto* qwen_tbl = (*trans)["qwen"].as_table())
                {
                    if (auto v = (*qwen_tbl)["api_key"].value<std::string>())
                        std::snprintf(global_translation_config_.qwen_api_key.data(), global_translation_config_.qwen_api_key.size(), "%s", v->c_str());
                    if (auto v = (*qwen_tbl)["model"].value<std::string>())
                        std::snprintf(global_translation_config_.qwen_model.data(), global_translation_config_.qwen_model.size(), "%s", v->c_str());
                }
                if (auto* niutrans_tbl = (*trans)["niutrans"].as_table())
                {
                    if (auto v = (*niutrans_tbl)["api_key"].value<std::string>())
                        std::snprintf(global_translation_config_.niutrans_api_key.data(), global_translation_config_.niutrans_api_key.size(), "%s", v->c_str());
                }
                if (auto* zhipu_tbl = (*trans)["zhipu"].as_table())
                {
                    if (auto v = (*zhipu_tbl)["base_url"].value<std::string>())
                        std::snprintf(global_translation_config_.zhipu_base_url.data(), global_translation_config_.zhipu_base_url.size(), "%s", v->c_str());
                    if (auto v = (*zhipu_tbl)["model"].value<std::string>())
                        std::snprintf(global_translation_config_.zhipu_model.data(), global_translation_config_.zhipu_model.size(), "%s", v->c_str());
                    if (auto v = (*zhipu_tbl)["api_key"].value<std::string>())
                        std::snprintf(global_translation_config_.zhipu_api_key.data(), global_translation_config_.zhipu_api_key.size(), "%s", v->c_str());
                }
                if (auto* youdao_tbl = (*trans)["youdao"].as_table())
                {
                    if (auto v = (*youdao_tbl)["app_key"].value<std::string>())
                        std::snprintf(global_translation_config_.youdao_app_key.data(), global_translation_config_.youdao_app_key.size(), "%s", v->c_str());
                    if (auto v = (*youdao_tbl)["app_secret"].value<std::string>())
                        std::snprintf(global_translation_config_.youdao_app_secret.data(), global_translation_config_.youdao_app_secret.size(), "%s", v->c_str());
                    if (auto v = (*youdao_tbl)["mode"].value<int>())
                    {
                        if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                            global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                        else
                            global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::Text;
                    }
                }

                if (auto v = (*trans)["openai_base_url"].value<std::string>())
                    std::snprintf(global_translation_config_.openai_base_url.data(), global_translation_config_.openai_base_url.size(), "%s", v->c_str());
                if (auto v = (*trans)["openai_model"].value<std::string>())
                    std::snprintf(global_translation_config_.openai_model.data(), global_translation_config_.openai_model.size(), "%s", v->c_str());
                if (auto v = (*trans)["openai_api_key"].value<std::string>())
                    std::snprintf(global_translation_config_.openai_api_key.data(), global_translation_config_.openai_api_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["google_api_key"].value<std::string>())
                    std::snprintf(global_translation_config_.google_api_key.data(), global_translation_config_.google_api_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["qwen_api_key"].value<std::string>())
                    std::snprintf(global_translation_config_.qwen_api_key.data(), global_translation_config_.qwen_api_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["qwen_model"].value<std::string>())
                    std::snprintf(global_translation_config_.qwen_model.data(), global_translation_config_.qwen_model.size(), "%s", v->c_str());
                if (auto v = (*trans)["niutrans_api_key"].value<std::string>())
                    std::snprintf(global_translation_config_.niutrans_api_key.data(), global_translation_config_.niutrans_api_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["zhipu_base_url"].value<std::string>())
                    std::snprintf(global_translation_config_.zhipu_base_url.data(), global_translation_config_.zhipu_base_url.size(), "%s", v->c_str());
                if (auto v = (*trans)["zhipu_model"].value<std::string>())
                    std::snprintf(global_translation_config_.zhipu_model.data(), global_translation_config_.zhipu_model.size(), "%s", v->c_str());
                if (auto v = (*trans)["zhipu_api_key"].value<std::string>())
                    std::snprintf(global_translation_config_.zhipu_api_key.data(), global_translation_config_.zhipu_api_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["youdao_app_key"].value<std::string>())
                    std::snprintf(global_translation_config_.youdao_app_key.data(), global_translation_config_.youdao_app_key.size(), "%s", v->c_str());
                if (auto v = (*trans)["youdao_app_secret"].value<std::string>())
                    std::snprintf(global_translation_config_.youdao_app_secret.data(), global_translation_config_.youdao_app_secret.size(), "%s", v->c_str());
                if (auto v = (*trans)["youdao_mode"].value<int>())
                {
                    if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                        global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                    else
                        global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::Text;
                }
            }
            markGlobalTranslationDirty();
        }
        applyVerboseSetting();

        if (auto* arr = root["dialogs"].as_array())
        {
            std::vector<std::pair<std::string, DialogStateManager>> dialog_configs;
            for (auto&& node : *arr)
            {
                if (!node.is_table()) continue;
                auto tbl = *node.as_table();
                DialogStateManager state;
                state.applyDefaults();  // Start with defaults
                std::string name;
                if (tomlToDialogState(tbl, state, name))  // Overlay config values
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
                auto windows = registry_->windowsByType(UIWindowType::Dialog);
                int have = static_cast<int>(windows.size());
                int want = static_cast<int>(dialog_configs.size());

                if (want > have)
                {
                    for (int i = 0; i < want - have; ++i)
                        registry_->createDialogWindow();
                    windows = registry_->windowsByType(UIWindowType::Dialog);
                }
                else if (want < have)
                {
                    for (int i = have - 1; i >= want; --i)
                    {
                        registry_->removeWindow(windows[i]);
                    }
                    windows = registry_->windowsByType(UIWindowType::Dialog);
                }

                int n = std::min(static_cast<int>(windows.size()), want);
                for (int i = 0; i < n; ++i)
                {
                    auto* dw = dynamic_cast<DialogWindow*>(windows[i]);
                    if (!dw) continue;
                    dw->rename(dialog_configs[i].first.c_str());
                    
                    dw->state() = dialog_configs[i].second;
                    dw->reinitializePlaceholder();
                    
                    // Restore runtime-only state not persisted in config
                    dw->state().ui_state().window_size = ImVec2(dw->state().ui_state().width, dw->state().ui_state().height);
                    dw->state().ui_state().pending_resize = true;
                    dw->state().ui_state().pending_reposition = true;
                    dw->state().ui_state().font = nullptr;
                    dw->state().ui_state().font_base_size = 0.0f;
                    
                    // Rebind font pointers after state replacement
                    dw->refreshFontBinding();
                    dw->initTranslatorIfEnabled();
                }
            }
        }

        auto applyQuestConfig = [&](QuestWindow& quest_window, QuestStateManager& quest_state, const std::string& quest_name)
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
            quest_window.state().ui_state().window_size = ImVec2(
                quest_window.state().ui_state().width,
                quest_window.state().ui_state().height);
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
            std::string quest_name;
            if (!tomlToDialogState(quest_tbl, quest_state, quest_name))
            {
                utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                    "Skipped invalid quest window in configuration",
                    "Missing name for quest entry in config file.");
                return;
            }

            auto quests = registry_->windowsByType(UIWindowType::Quest);
            QuestWindow* quest_window = nullptr;
            if (index < quests.size())
            {
                quest_window = dynamic_cast<QuestWindow*>(quests[index]);
            }
            if (!quest_window)
            {
                quest_window = &registry_->createQuestWindow();
            }

            if (quest_window)
            {
                applyQuestConfig(*quest_window, quest_state, quest_name);
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
            if (registry_->windowsByType(UIWindowType::Quest).empty())
            {
                registry_->createQuestWindow();
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
            error_details = "Error at line " + std::to_string(pe.source().begin.line) + ": " + std::string(pe.description());
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

bool ConfigManager::loadAtStartup()
{
    return loadAndApply();
}

void ConfigManager::pollAndApply()
{
    fs::path p(config_path_);
    auto mtime = file_mtime_ms(p);
    if (mtime == 0 || mtime == last_mtime_)
        return;

    // Hot-reload: reapply config if file changed externally
    if (loadAndApply())
    {
        last_mtime_ = mtime;
        last_error_.clear();
        PLOG_INFO << "Config reloaded from " << config_path_;
    }
    else
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
            "Failed to reload configuration",
            last_error_.empty() ? std::string("See logs for details") : last_error_);
    }
}

void ConfigManager::setVerboseLogging(bool enabled)
{
    verbose_logging_ = enabled;
    applyVerboseSetting();
}

void ConfigManager::setForceVerboseLogging(bool enabled)
{
    force_verbose_logging_ = enabled;
    applyVerboseSetting();
}

void ConfigManager::applyVerboseSetting()
{
    processing::Diagnostics::SetVerbose(force_verbose_logging_ || verbose_logging_);
}

void ConfigManager::markGlobalTranslationDirty()
{
    ++global_translation_version_;
    if (global_translation_version_ == 0)
    {
        ++global_translation_version_;
    }
}

ConfigManager* ConfigManager_Get() { return g_cfg_mgr; }
void ConfigManager_Set(ConfigManager* mgr) { g_cfg_mgr = mgr; }

bool ConfigManager_SaveAll()
{
    if (g_cfg_mgr) return g_cfg_mgr->saveAll();
    return false;
}
