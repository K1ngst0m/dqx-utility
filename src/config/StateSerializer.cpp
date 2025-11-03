#include "StateSerializer.hpp"

#include "../ui/common/BaseWindowState.hpp"
#include "../ui/GlobalStateManager.hpp"
#include "../ui/Localization.hpp"
#include "translate/TranslationConfig.hpp"

#include <array>

namespace
{
// Safe string copy with guaranteed null termination
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
} // namespace

toml::table StateSerializer::serialize(const std::string& name, const BaseWindowState& state)
{
    toml::table t;
    t.insert("name", name);
    
    serializeCommonState(t, state);
    
    return t;
}

void StateSerializer::serializeCommonState(toml::table& t, const BaseWindowState& state)
{
    // Behavior section
    toml::table behavior;
    behavior.insert("auto_scroll_to_new", state.ui_state().auto_scroll_to_new);
    t.insert("behavior", std::move(behavior));

    // Translation section
    toml::table translation;
    translation.insert("use_global_translation", state.use_global_translation);
    translation.insert("translate_enabled", state.translation_config().translate_enabled);
    translation.insert("auto_apply_changes", state.translation_config().auto_apply_changes);
    translation.insert("include_dialog_stream", state.translation_config().include_dialog_stream);
    translation.insert("include_corner_stream", state.translation_config().include_corner_stream);
    translation.insert("glossary_enabled", state.translation_config().glossary_enabled);
    translation.insert("fuzzy_glossary_enabled", state.translation_config().fuzzy_glossary_enabled);
    translation.insert("fuzzy_glossary_threshold", state.translation_config().fuzzy_glossary_threshold);
    translation.insert("translation_backend", static_cast<int>(state.translation_config().translation_backend));

    std::string target_lang;
    switch (state.translation_config().target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US:
        target_lang = "en-us";
        break;
    case TranslationConfig::TargetLang::ZH_CN:
        target_lang = "zh-cn";
        break;
    case TranslationConfig::TargetLang::ZH_TW:
        target_lang = "zh-tw";
        break;
    }
    translation.insert("target_lang", target_lang);
    translation.insert("custom_prompt", std::string(state.translation_config().custom_prompt.data()));

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

    // Appearance section
    toml::table appearance;
    appearance.insert("width", state.ui_state().width);
    appearance.insert("height", state.ui_state().height);
    appearance.insert("pos_x", state.ui_state().window_pos.x);
    appearance.insert("pos_y", state.ui_state().window_pos.y);
    appearance.insert("padding_x", state.ui_state().padding.x);
    appearance.insert("padding_y", state.ui_state().padding.y);
    appearance.insert("rounding", state.ui_state().rounding);
    appearance.insert("border_thickness", state.ui_state().border_thickness);
    appearance.insert("border_enabled", state.ui_state().border_enabled);
    appearance.insert("background_alpha", state.ui_state().background_alpha);
    appearance.insert("font_size", state.ui_state().font_size);
    appearance.insert("vignette_thickness", state.ui_state().vignette_thickness);
    appearance.insert("font_path", std::string(state.ui_state().font_path.data()));
    appearance.insert("fade_enabled", state.ui_state().fade_enabled);
    appearance.insert("fade_timeout", state.ui_state().fade_timeout);
    t.insert("appearance", std::move(appearance));
}

bool StateSerializer::deserialize(const toml::table& t, BaseWindowState& state, std::string& name)
{
    auto name_val = t["name"].value<std::string>();
    if (!name_val)
        return false;
    name = *name_val;

    deserializeCommonState(t, state);
    
    return true;
}

void StateSerializer::deserializeCommonState(const toml::table& t, BaseWindowState& state)
{
    // Behavior section
    if (auto* behavior = t["behavior"].as_table())
    {
        if (auto v = (*behavior)["auto_scroll_to_new"].value<bool>())
            state.ui_state().auto_scroll_to_new = *v;
    }

    // Translation section
    if (auto* translation_tbl = t["translation"].as_table())
    {
        if (auto v = (*translation_tbl)["use_global_translation"].value<bool>())
            state.use_global_translation = *v;
        if (auto v = (*translation_tbl)["translate_enabled"].value<bool>())
            state.translation_config().translate_enabled = *v;
        if (auto v = (*translation_tbl)["auto_apply_changes"].value<bool>())
            state.translation_config().auto_apply_changes = *v;
        if (auto v = (*translation_tbl)["include_dialog_stream"].value<bool>())
            state.translation_config().include_dialog_stream = *v;
        if (auto v = (*translation_tbl)["include_corner_stream"].value<bool>())
            state.translation_config().include_corner_stream = *v;
        if (auto v = (*translation_tbl)["glossary_enabled"].value<bool>())
            state.translation_config().glossary_enabled = *v;
        if (auto v = (*translation_tbl)["fuzzy_glossary_enabled"].value<bool>())
            state.translation_config().fuzzy_glossary_enabled = *v;
        if (auto v = (*translation_tbl)["fuzzy_glossary_threshold"].value<double>())
            state.translation_config().fuzzy_glossary_threshold = static_cast<float>(*v);
        if (auto v = (*translation_tbl)["translation_backend"].value<int>())
            state.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(*v);
        if (auto v = (*translation_tbl)["target_lang"].value<std::string>())
        {
            if (*v == "en-us")
                state.translation_config().target_lang_enum = TranslationConfig::TargetLang::EN_US;
            else if (*v == "zh-cn")
                state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
            else if (*v == "zh-tw")
                state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
        }
        if (auto v = (*translation_tbl)["custom_prompt"].value<std::string>())
            safe_strncpy(state.translation_config().custom_prompt.data(), v->c_str(),
                         state.translation_config().custom_prompt.size());

        if (auto* openai_tbl = (*translation_tbl)["openai"].as_table())
        {
            if (auto v = (*openai_tbl)["base_url"].value<std::string>())
                safe_strncpy(state.translation_config().openai_base_url.data(), v->c_str(),
                             state.translation_config().openai_base_url.size());
            if (auto v = (*openai_tbl)["model"].value<std::string>())
                safe_strncpy(state.translation_config().openai_model.data(), v->c_str(),
                             state.translation_config().openai_model.size());
            if (auto v = (*openai_tbl)["api_key"].value<std::string>())
                safe_strncpy(state.translation_config().openai_api_key.data(), v->c_str(),
                             state.translation_config().openai_api_key.size());
        }

        if (auto* google_tbl = (*translation_tbl)["google"].as_table())
        {
            if (auto v = (*google_tbl)["api_key"].value<std::string>())
                safe_strncpy(state.translation_config().google_api_key.data(), v->c_str(),
                             state.translation_config().google_api_key.size());
        }

        if (auto* qwen_tbl = (*translation_tbl)["qwen"].as_table())
        {
            if (auto v = (*qwen_tbl)["api_key"].value<std::string>())
                safe_strncpy(state.translation_config().qwen_api_key.data(), v->c_str(),
                             state.translation_config().qwen_api_key.size());
            if (auto v = (*qwen_tbl)["model"].value<std::string>())
                safe_strncpy(state.translation_config().qwen_model.data(), v->c_str(),
                             state.translation_config().qwen_model.size());
        }

        if (auto* niutrans_tbl = (*translation_tbl)["niutrans"].as_table())
        {
            if (auto v = (*niutrans_tbl)["api_key"].value<std::string>())
                safe_strncpy(state.translation_config().niutrans_api_key.data(), v->c_str(),
                             state.translation_config().niutrans_api_key.size());
        }

        if (auto* zhipu_tbl = (*translation_tbl)["zhipu"].as_table())
        {
            if (auto v = (*zhipu_tbl)["base_url"].value<std::string>())
                safe_strncpy(state.translation_config().zhipu_base_url.data(), v->c_str(),
                             state.translation_config().zhipu_base_url.size());
            if (auto v = (*zhipu_tbl)["model"].value<std::string>())
                safe_strncpy(state.translation_config().zhipu_model.data(), v->c_str(),
                             state.translation_config().zhipu_model.size());
            if (auto v = (*zhipu_tbl)["api_key"].value<std::string>())
                safe_strncpy(state.translation_config().zhipu_api_key.data(), v->c_str(),
                             state.translation_config().zhipu_api_key.size());
        }

        if (auto* youdao_tbl = (*translation_tbl)["youdao"].as_table())
        {
            if (auto v = (*youdao_tbl)["app_key"].value<std::string>())
                safe_strncpy(state.translation_config().youdao_app_key.data(), v->c_str(),
                             state.translation_config().youdao_app_key.size());
            if (auto v = (*youdao_tbl)["app_secret"].value<std::string>())
                safe_strncpy(state.translation_config().youdao_app_secret.data(), v->c_str(),
                             state.translation_config().youdao_app_secret.size());
            if (auto v = (*youdao_tbl)["mode"].value<int>())
            {
                if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                    state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                else
                    state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::Text;
            }
        }
    }

    // Appearance section
    if (auto* appearance = t["appearance"].as_table())
    {
        if (auto v = (*appearance)["width"].value<double>())
            state.ui_state().width = static_cast<float>(*v);
        if (auto v = (*appearance)["height"].value<double>())
            state.ui_state().height = static_cast<float>(*v);
        if (auto v = (*appearance)["pos_x"].value<double>())
            state.ui_state().window_pos.x = static_cast<float>(*v);
        if (auto v = (*appearance)["pos_y"].value<double>())
            state.ui_state().window_pos.y = static_cast<float>(*v);
        if (auto v = (*appearance)["padding_x"].value<double>())
            state.ui_state().padding.x = static_cast<float>(*v);
        if (auto v = (*appearance)["padding_y"].value<double>())
            state.ui_state().padding.y = static_cast<float>(*v);
        if (auto v = (*appearance)["rounding"].value<double>())
            state.ui_state().rounding = static_cast<float>(*v);
        if (auto v = (*appearance)["border_thickness"].value<double>())
            state.ui_state().border_thickness = static_cast<float>(*v);
        if (auto v = (*appearance)["border_enabled"].value<bool>())
            state.ui_state().border_enabled = *v;
        if (auto v = (*appearance)["background_alpha"].value<double>())
            state.ui_state().background_alpha = static_cast<float>(*v);
        if (auto v = (*appearance)["font_size"].value<double>())
            state.ui_state().font_size = static_cast<float>(*v);
        if (auto v = (*appearance)["vignette_thickness"].value<double>())
            state.ui_state().vignette_thickness = static_cast<float>(*v);
        if (auto v = (*appearance)["font_path"].value<std::string>())
            safe_strncpy(state.ui_state().font_path.data(), v->c_str(), state.ui_state().font_path.size());
        if (auto v = (*appearance)["fade_enabled"].value<bool>())
            state.ui_state().fade_enabled = *v;
        if (auto v = (*appearance)["fade_timeout"].value<double>())
            state.ui_state().fade_timeout = static_cast<float>(*v);
    }

    // Legacy flat structure support (backward compatibility for old config files)
    if (auto v = t["auto_scroll_to_new"].value<bool>())
        state.ui_state().auto_scroll_to_new = *v;
    if (auto v = t["use_global_translation"].value<bool>())
        state.use_global_translation = *v;
    if (auto v = t["translate_enabled"].value<bool>())
        state.translation_config().translate_enabled = *v;
    if (auto v = t["auto_apply_changes"].value<bool>())
        state.translation_config().auto_apply_changes = *v;
    if (auto v = t["include_dialog_stream"].value<bool>())
        state.translation_config().include_dialog_stream = *v;
    if (auto v = t["include_corner_stream"].value<bool>())
        state.translation_config().include_corner_stream = *v;
    if (auto v = t["glossary_enabled"].value<bool>())
        state.translation_config().glossary_enabled = *v;
    if (auto v = t["fuzzy_glossary_enabled"].value<bool>())
        state.translation_config().fuzzy_glossary_enabled = *v;
    if (auto v = t["fuzzy_glossary_threshold"].value<double>())
        state.translation_config().fuzzy_glossary_threshold = static_cast<float>(*v);
    if (auto v = t["translation_backend"].value<int>())
        state.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(*v);

    if (auto v = t["target_lang"].value<std::string>())
    {
        if (*v == "en-us")
            state.translation_config().target_lang_enum = TranslationConfig::TargetLang::EN_US;
        else if (*v == "zh-cn")
            state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
        else if (*v == "zh-tw")
            state.translation_config().target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
    }
    if (auto v = t["custom_prompt"].value<std::string>())
        safe_strncpy(state.translation_config().custom_prompt.data(), v->c_str(),
                     state.translation_config().custom_prompt.size());

    if (auto v = t["openai_base_url"].value<std::string>())
        safe_strncpy(state.translation_config().openai_base_url.data(), v->c_str(),
                     state.translation_config().openai_base_url.size());
    if (auto v = t["openai_model"].value<std::string>())
        safe_strncpy(state.translation_config().openai_model.data(), v->c_str(),
                     state.translation_config().openai_model.size());
    if (auto v = t["openai_api_key"].value<std::string>())
        safe_strncpy(state.translation_config().openai_api_key.data(), v->c_str(),
                     state.translation_config().openai_api_key.size());
    if (auto v = t["google_api_key"].value<std::string>())
        safe_strncpy(state.translation_config().google_api_key.data(), v->c_str(),
                     state.translation_config().google_api_key.size());
    if (auto v = t["qwen_api_key"].value<std::string>())
        safe_strncpy(state.translation_config().qwen_api_key.data(), v->c_str(),
                     state.translation_config().qwen_api_key.size());
    if (auto v = t["qwen_model"].value<std::string>())
        safe_strncpy(state.translation_config().qwen_model.data(), v->c_str(),
                     state.translation_config().qwen_model.size());
    if (auto v = t["niutrans_api_key"].value<std::string>())
        safe_strncpy(state.translation_config().niutrans_api_key.data(), v->c_str(),
                     state.translation_config().niutrans_api_key.size());
    if (auto v = t["zhipu_base_url"].value<std::string>())
        safe_strncpy(state.translation_config().zhipu_base_url.data(), v->c_str(),
                     state.translation_config().zhipu_base_url.size());
    if (auto v = t["zhipu_model"].value<std::string>())
        safe_strncpy(state.translation_config().zhipu_model.data(), v->c_str(),
                     state.translation_config().zhipu_model.size());
    if (auto v = t["zhipu_api_key"].value<std::string>())
        safe_strncpy(state.translation_config().zhipu_api_key.data(), v->c_str(),
                     state.translation_config().zhipu_api_key.size());
    if (auto v = t["youdao_app_key"].value<std::string>())
        safe_strncpy(state.translation_config().youdao_app_key.data(), v->c_str(),
                     state.translation_config().youdao_app_key.size());
    if (auto v = t["youdao_app_secret"].value<std::string>())
        safe_strncpy(state.translation_config().youdao_app_secret.data(), v->c_str(),
                     state.translation_config().youdao_app_secret.size());
    if (auto v = t["youdao_mode"].value<int>())
    {
        if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
            state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
        else
            state.translation_config().youdao_mode = TranslationConfig::YoudaoMode::Text;
    }

    // GUI properties
    if (auto v = t["width"].value<double>())
        state.ui_state().width = static_cast<float>(*v);
    if (auto v = t["height"].value<double>())
        state.ui_state().height = static_cast<float>(*v);
    if (auto v = t["pos_x"].value<double>())
        state.ui_state().window_pos.x = static_cast<float>(*v);
    if (auto v = t["pos_y"].value<double>())
        state.ui_state().window_pos.y = static_cast<float>(*v);
    if (auto v = t["padding_x"].value<double>())
        state.ui_state().padding.x = static_cast<float>(*v);
    if (auto v = t["padding_y"].value<double>())
        state.ui_state().padding.y = static_cast<float>(*v);
    if (auto v = t["rounding"].value<double>())
        state.ui_state().rounding = static_cast<float>(*v);
    if (auto v = t["border_thickness"].value<double>())
        state.ui_state().border_thickness = static_cast<float>(*v);
    if (auto v = t["background_alpha"].value<double>())
        state.ui_state().background_alpha = static_cast<float>(*v);
    if (auto v = t["font_size"].value<double>())
        state.ui_state().font_size = static_cast<float>(*v);
    if (auto v = t["vignette_thickness"].value<double>())
        state.ui_state().vignette_thickness = static_cast<float>(*v);
    if (auto v = t["font_path"].value<std::string>())
        safe_strncpy(state.ui_state().font_path.data(), v->c_str(), state.ui_state().font_path.size());

    // Fade settings
    if (auto v = t["fade_enabled"].value<bool>())
        state.ui_state().fade_enabled = *v;
    if (auto v = t["fade_timeout"].value<double>())
        state.ui_state().fade_timeout = static_cast<float>(*v);
}

toml::table StateSerializer::serializeGlobal(const GlobalStateManager& state)
{
    toml::table root;
    
    // [global] section
    toml::table global;
    global.insert("ui_scale", state.uiScale());
    global.insert("append_logs", state.appendLogs());
    global.insert("borderless_windows", state.borderlessWindows());
    global.insert("app_mode", static_cast<int>(state.appMode()));
    global.insert("window_always_on_top", state.windowAlwaysOnTop());
    global.insert("ui_language", state.uiLanguage());
    global.insert("default_dialog_enabled", state.defaultDialogEnabled());
    global.insert("default_quest_enabled", state.defaultQuestEnabled());
    global.insert("default_quest_helper_enabled", state.defaultQuestHelperEnabled());

    // [global.translation] section
    toml::table translation;
    translation.insert("translate_enabled", state.translationConfig().translate_enabled);
    translation.insert("auto_apply_changes", state.translationConfig().auto_apply_changes);
    translation.insert("include_dialog_stream", state.translationConfig().include_dialog_stream);
    translation.insert("include_corner_stream", state.translationConfig().include_corner_stream);
    translation.insert("translation_backend", static_cast<int>(state.translationConfig().translation_backend));

    std::string target_lang;
    switch (state.translationConfig().target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US:
        target_lang = "en-us";
        break;
    case TranslationConfig::TargetLang::ZH_CN:
        target_lang = "zh-cn";
        break;
    case TranslationConfig::TargetLang::ZH_TW:
        target_lang = "zh-tw";
        break;
    }
    translation.insert("target_lang", target_lang);
    translation.insert("custom_prompt", std::string(state.translationConfig().custom_prompt.data()));

    // Backend-specific API configs
    toml::table openai;
    openai.insert("api_key", std::string(state.translationConfig().openai_api_key.data()));
    openai.insert("base_url", std::string(state.translationConfig().openai_base_url.data()));
    openai.insert("model", std::string(state.translationConfig().openai_model.data()));
    translation.insert("openai", std::move(openai));

    toml::table google;
    google.insert("api_key", std::string(state.translationConfig().google_api_key.data()));
    translation.insert("google", std::move(google));

    toml::table qwen;
    qwen.insert("api_key", std::string(state.translationConfig().qwen_api_key.data()));
    qwen.insert("model", std::string(state.translationConfig().qwen_model.data()));
    translation.insert("qwen", std::move(qwen));

    toml::table niutrans;
    niutrans.insert("api_key", std::string(state.translationConfig().niutrans_api_key.data()));
    translation.insert("niutrans", std::move(niutrans));

    toml::table zhipu;
    zhipu.insert("api_key", std::string(state.translationConfig().zhipu_api_key.data()));
    zhipu.insert("base_url", std::string(state.translationConfig().zhipu_base_url.data()));
    zhipu.insert("model", std::string(state.translationConfig().zhipu_model.data()));
    translation.insert("zhipu", std::move(zhipu));

    toml::table youdao;
    youdao.insert("app_key", std::string(state.translationConfig().youdao_app_key.data()));
    youdao.insert("app_secret", std::string(state.translationConfig().youdao_app_secret.data()));
    youdao.insert("mode", static_cast<int>(state.translationConfig().youdao_mode));
    translation.insert("youdao", std::move(youdao));

    global.insert("translation", std::move(translation));
    root.insert("global", std::move(global));

    // [app.debug] section
    toml::table app;
    toml::table debug;
    debug.insert("profiling_level", state.profilingLevel());
    debug.insert("logging_level", state.loggingLevel());
    debug.insert("verbose", state.verbose());
    debug.insert("compatibility_mode", state.compatibilityMode());
    debug.insert("hook_wait_timeout_ms", state.hookWaitTimeoutMs());
    app.insert("debug", std::move(debug));
    root.insert("app", std::move(app));

    return root;
}

void StateSerializer::deserializeGlobal(const toml::table& root, GlobalStateManager& state)
{
    // Parse [global] section
    if (auto* g = root["global"].as_table())
    {
        if (auto v = (*g)["ui_scale"].value<float>())
            state.setUIScale(*v);
        if (auto v = (*g)["append_logs"].value<bool>())
            state.setAppendLogs(*v);
        if (auto v = (*g)["borderless_windows"].value<bool>())
            state.setBorderlessWindows(*v);
        if (auto v = (*g)["app_mode"].value<int>())
            state.setAppMode(static_cast<GlobalStateManager::AppMode>(*v));
        if (auto v = (*g)["window_always_on_top"].value<bool>())
            state.setWindowAlwaysOnTop(*v);
        if (auto v = (*g)["ui_language"].value<std::string>())
            state.setUILanguage(*v);
        if (auto v = (*g)["default_dialog_enabled"].value<bool>())
            state.setDefaultDialogEnabled(*v);
        if (auto v = (*g)["default_quest_enabled"].value<bool>())
            state.setDefaultQuestEnabled(*v);
        if (auto v = (*g)["default_quest_helper_enabled"].value<bool>())
            state.setDefaultQuestHelperEnabled(*v);

        // Initialize translation defaults before parsing
        state.translationConfig().applyDefaults();
        if (state.translationConfig().custom_prompt[0] == '\0')
        {
            safe_strncpy(state.translationConfig().custom_prompt.data(),
                         i18n::get("dialog.settings.default_prompt"),
                         state.translationConfig().custom_prompt.size());
        }

        // Parse [global.translation] section
        if (auto* trans = (*g)["translation"].as_table())
        {
            if (auto v = (*trans)["translate_enabled"].value<bool>())
                state.translationConfig().translate_enabled = *v;
            if (auto v = (*trans)["auto_apply_changes"].value<bool>())
                state.translationConfig().auto_apply_changes = *v;
            if (auto v = (*trans)["include_dialog_stream"].value<bool>())
                state.translationConfig().include_dialog_stream = *v;
            if (auto v = (*trans)["include_corner_stream"].value<bool>())
                state.translationConfig().include_corner_stream = *v;
            if (auto v = (*trans)["translation_backend"].value<int>())
                state.translationConfig().translation_backend =
                    static_cast<TranslationConfig::TranslationBackend>(*v);
            if (auto v = (*trans)["target_lang"].value<std::string>())
            {
                if (*v == "en-us")
                    state.translationConfig().target_lang_enum = TranslationConfig::TargetLang::EN_US;
                else if (*v == "zh-cn")
                    state.translationConfig().target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
                else if (*v == "zh-tw")
                    state.translationConfig().target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
            }
            if (auto v = (*trans)["custom_prompt"].value<std::string>())
                safe_strncpy(state.translationConfig().custom_prompt.data(), v->c_str(),
                             state.translationConfig().custom_prompt.size());

            // Backend-specific configs
            if (auto* openai_tbl = (*trans)["openai"].as_table())
            {
                if (auto v = (*openai_tbl)["base_url"].value<std::string>())
                    safe_strncpy(state.translationConfig().openai_base_url.data(), v->c_str(),
                                 state.translationConfig().openai_base_url.size());
                if (auto v = (*openai_tbl)["model"].value<std::string>())
                    safe_strncpy(state.translationConfig().openai_model.data(), v->c_str(),
                                 state.translationConfig().openai_model.size());
                if (auto v = (*openai_tbl)["api_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().openai_api_key.data(), v->c_str(),
                                 state.translationConfig().openai_api_key.size());
            }

            if (auto* google_tbl = (*trans)["google"].as_table())
            {
                if (auto v = (*google_tbl)["api_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().google_api_key.data(), v->c_str(),
                                 state.translationConfig().google_api_key.size());
            }

            if (auto* qwen_tbl = (*trans)["qwen"].as_table())
            {
                if (auto v = (*qwen_tbl)["api_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().qwen_api_key.data(), v->c_str(),
                                 state.translationConfig().qwen_api_key.size());
                if (auto v = (*qwen_tbl)["model"].value<std::string>())
                    safe_strncpy(state.translationConfig().qwen_model.data(), v->c_str(),
                                 state.translationConfig().qwen_model.size());
            }

            if (auto* niutrans_tbl = (*trans)["niutrans"].as_table())
            {
                if (auto v = (*niutrans_tbl)["api_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().niutrans_api_key.data(), v->c_str(),
                                 state.translationConfig().niutrans_api_key.size());
            }

            if (auto* zhipu_tbl = (*trans)["zhipu"].as_table())
            {
                if (auto v = (*zhipu_tbl)["base_url"].value<std::string>())
                    safe_strncpy(state.translationConfig().zhipu_base_url.data(), v->c_str(),
                                 state.translationConfig().zhipu_base_url.size());
                if (auto v = (*zhipu_tbl)["model"].value<std::string>())
                    safe_strncpy(state.translationConfig().zhipu_model.data(), v->c_str(),
                                 state.translationConfig().zhipu_model.size());
                if (auto v = (*zhipu_tbl)["api_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().zhipu_api_key.data(), v->c_str(),
                                 state.translationConfig().zhipu_api_key.size());
            }

            if (auto* youdao_tbl = (*trans)["youdao"].as_table())
            {
                if (auto v = (*youdao_tbl)["app_key"].value<std::string>())
                    safe_strncpy(state.translationConfig().youdao_app_key.data(), v->c_str(),
                                 state.translationConfig().youdao_app_key.size());
                if (auto v = (*youdao_tbl)["app_secret"].value<std::string>())
                    safe_strncpy(state.translationConfig().youdao_app_secret.data(), v->c_str(),
                                 state.translationConfig().youdao_app_secret.size());
                if (auto v = (*youdao_tbl)["mode"].value<int>())
                {
                    if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                        state.translationConfig().youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                    else
                        state.translationConfig().youdao_mode = TranslationConfig::YoudaoMode::Text;
                }
            }
        }

        state.incrementTranslationVersion();
    }

    // Parse [app.debug] section
    if (auto* app = root["app"].as_table())
    {
        if (auto* dbg = (*app)["debug"].as_table())
        {
            if (auto v = (*dbg)["profiling_level"].value<int>())
                state.setProfilingLevel(*v);
            if (auto v = (*dbg)["logging_level"].value<int>())
                state.setLoggingLevel(*v);
            if (auto v = (*dbg)["verbose"].value<bool>())
                state.setVerbose(*v);
            if (auto v = (*dbg)["compatibility_mode"].value<bool>())
                state.setCompatibilityMode(*v);
            if (auto v = (*dbg)["hook_wait_timeout_ms"].value<int>())
                state.setHookWaitTimeoutMs(*v);
        }
    }
}
