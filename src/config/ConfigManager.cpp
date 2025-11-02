#include "ConfigManager.hpp"

#include "DefaultWindowManager.hpp"
#include "WindowStateOperations.hpp"
#include "StateSerializer.hpp"
#include "WindowStateApplier.hpp"
#include "../ui/WindowRegistry.hpp"
#include "../ui/dialog/DialogWindow.hpp"
#include "../ui/quest/QuestWindow.hpp"
#include "../ui/quest/QuestHelperWindow.hpp"
#include "../ui/Localization.hpp"
#include "../ui/common/BaseWindowState.hpp"
#include "../ui/dialog/DialogStateManager.hpp"
#include "../ui/quest/QuestStateManager.hpp"
#include "../ui/quest/QuestHelperStateManager.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../processing/Diagnostics.hpp"
#include "../utils/Profile.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <imgui.h>

namespace fs = std::filesystem;

namespace
{
/// Safe string copy with guaranteed null termination
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

struct ScopedFlagGuard
{
    bool& flag;
    bool previous;
    bool active;

    ScopedFlagGuard(bool& target, bool value)
        : flag(target)
        , previous(target)
        , active(true)
    {
        flag = value;
    }

    ~ScopedFlagGuard()
    {
        if (active)
            flag = previous;
    }

    void restore()
    {
        if (!active)
            return;
        flag = previous;
        active = false;
    }
};
} // namespace

static ConfigManager* g_cfg_mgr = nullptr;


static long long file_mtime_ms(const fs::path& p)
{
    std::error_code ec;
    auto tp = fs::last_write_time(p, ec);
    if (ec)
        return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
}

ConfigManager::ConfigManager()
{
    config_path_ = "config.toml";
    last_mtime_ = file_mtime_ms(config_path_);
    global_translation_config_.applyDefaults();
    
    // Initialize window managers (will be configured when registry is set)
    // Note: These are initialized without operations yet - will be set in setRegistry()
    
    // Logging level will be applied when config is loaded via setLoggingLevel()
}

ConfigManager::~ConfigManager() = default;

void ConfigManager::setDefaultDialogEnabled(bool enabled)
{
    if (default_dialog_enabled_ == enabled)
        return;
    
    default_dialog_enabled_ = enabled;
    
    if (default_dialog_mgr_)
    {
        default_dialog_mgr_->setEnabled(enabled, suppress_default_window_updates_, registry_);
        
        if (!suppress_default_window_updates_ && registry_)
            saveAll();
    }
}

void ConfigManager::setDefaultQuestEnabled(bool enabled)
{
    if (default_quest_enabled_ == enabled)
        return;
    
    default_quest_enabled_ = enabled;
    
    if (default_quest_mgr_)
    {
        default_quest_mgr_->setEnabled(enabled, suppress_default_window_updates_, registry_);
        
        if (!suppress_default_window_updates_ && registry_)
            saveAll();
    }
}

void ConfigManager::setDefaultQuestHelperEnabled(bool enabled)
{
    if (default_quest_helper_enabled_ == enabled)
        return;
    
    default_quest_helper_enabled_ = enabled;
    
    if (default_quest_helper_mgr_)
    {
        default_quest_helper_mgr_->setEnabled(enabled, suppress_default_window_updates_, registry_);
        
        if (!suppress_default_window_updates_ && registry_)
            saveAll();
    }
}

void ConfigManager::reconcileDefaultWindowStates()
{
    enforceDefaultWindowStates();
}




void ConfigManager::enforceDefaultWindowStates()
{
    if (default_dialog_mgr_)
        default_dialog_mgr_->enforceState(registry_);
    if (default_quest_mgr_)
        default_quest_mgr_->enforceState(registry_);
    if (default_quest_helper_mgr_)
        default_quest_helper_mgr_->enforceState(registry_);
}

void ConfigManager::setUIScale(float scale)
{
    if (scale <= 0.1f)
        scale = 0.1f;
    if (scale > 3.0f)
        scale = 3.0f;
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
    
    if (registry_)
    {
        // Initialize window managers with their operations
        auto dialog_ops = std::make_unique<WindowStateOperations<DialogWindow, DialogStateManager>>(registry_, UIWindowType::Dialog);
        default_dialog_mgr_ = std::make_unique<DefaultWindowManager>(std::move(dialog_ops), "dialogs");
        
        auto quest_ops = std::make_unique<WindowStateOperations<QuestWindow, QuestStateManager>>(registry_, UIWindowType::Quest);
        default_quest_mgr_ = std::make_unique<DefaultWindowManager>(std::move(quest_ops), "quests");
        
        auto quest_helper_ops = std::make_unique<WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>>(registry_, UIWindowType::QuestHelper);
        default_quest_helper_mgr_ = std::make_unique<DefaultWindowManager>(std::move(quest_helper_ops), "quest_helpers");
        
        // Sync enabled states to newly created managers
        if (default_dialog_mgr_)
        {
            default_dialog_mgr_->setEnabled(default_dialog_enabled_, true, registry_);
        }
        if (default_quest_mgr_)
        {
            default_quest_mgr_->setEnabled(default_quest_enabled_, true, registry_);
        }
        if (default_quest_helper_mgr_)
        {
            default_quest_helper_mgr_->setEnabled(default_quest_helper_enabled_, true, registry_);
        }
    }
}

bool ConfigManager::saveAll()
{
    last_error_.clear();
    if (!registry_)
    {
        last_error_ = "No registry assigned";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Unable to save configuration",
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
    global.insert("default_dialog_enabled", default_dialog_enabled_);
    global.insert("default_quest_enabled", default_quest_enabled_);
    global.insert("default_quest_helper_enabled", default_quest_helper_enabled_);

    toml::table translation;
    translation.insert("translate_enabled", global_translation_config_.translate_enabled);
    translation.insert("auto_apply_changes", global_translation_config_.auto_apply_changes);
    translation.insert("include_dialog_stream", global_translation_config_.include_dialog_stream);
    translation.insert("include_corner_stream", global_translation_config_.include_corner_stream);
    translation.insert("translation_backend", static_cast<int>(global_translation_config_.translation_backend));
    std::string global_target_lang;
    switch (global_translation_config_.target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US:
        global_target_lang = "en-us";
        break;
    case TranslationConfig::TargetLang::ZH_CN:
        global_target_lang = "zh-cn";
        break;
    case TranslationConfig::TargetLang::ZH_TW:
        global_target_lang = "zh-tw";
        break;
    }
    translation.insert("target_lang", global_target_lang);
    translation.insert("custom_prompt", std::string(global_translation_config_.custom_prompt.data()));

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

    // [app.debug] section
    toml::table app;
    toml::table debug;
    debug.insert("profiling_level", profiling_level_);
    debug.insert("logging_level", logging_level_);
    debug.insert("verbose", verbose_);
    debug.insert("compatibility_mode", compatibility_mode_);
    debug.insert("hook_wait_timeout_ms", hook_wait_timeout_ms_);
    app.insert("debug", std::move(debug));
    root.insert("app", std::move(app));

    auto windows = registry_->windowsByType(UIWindowType::Dialog);
    toml::array arr;
    for (auto* w : windows)
    {
        auto* dw = dynamic_cast<DialogWindow*>(w);
        if (!dw)
            continue;
        auto t = StateSerializer::serialize(dw->displayName(), dw->state());
        arr.push_back(std::move(t));
    }
    root.insert("dialogs", std::move(arr));

    auto quest_windows = registry_->windowsByType(UIWindowType::Quest);
    toml::array quests_arr;
    for (auto* w : quest_windows)
    {
        auto* qw = dynamic_cast<QuestWindow*>(w);
        if (!qw)
            continue;
        quests_arr.push_back(StateSerializer::serialize(qw->displayName(), qw->state()));
    }
    if (!quests_arr.empty())
    {
        root.insert("quests", std::move(quests_arr));
    }

    auto quest_helper_windows = registry_->windowsByType(UIWindowType::QuestHelper);
    toml::array quest_helpers_arr;
    for (auto* w : quest_helper_windows)
    {
        auto* qhw = dynamic_cast<QuestHelperWindow*>(w);
        if (!qhw)
            continue;
        quest_helpers_arr.push_back(StateSerializer::serialize(qhw->displayName(), qhw->state()));
    }
    if (!quest_helpers_arr.empty())
    {
        root.insert("quest_helpers", std::move(quest_helpers_arr));
    }

    std::string tmp = config_path_ + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs)
    {
        last_error_ = "Failed to open temp file for writing";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
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
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
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
    ScopedFlagGuard guard(suppress_default_window_updates_, true);
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
            if (auto v = (*g)["default_dialog_enabled"].value<bool>())
                setDefaultDialogEnabled(*v);
            if (auto v = (*g)["default_quest_enabled"].value<bool>())
                setDefaultQuestEnabled(*v);
            if (auto v = (*g)["default_quest_helper_enabled"].value<bool>())
                setDefaultQuestHelperEnabled(*v);

            global_translation_config_.applyDefaults();
            if (global_translation_config_.custom_prompt[0] == '\0')
            {
                safe_strncpy(global_translation_config_.custom_prompt.data(),
                             i18n::get("dialog.settings.default_prompt"),
                             global_translation_config_.custom_prompt.size());
            }
            if (auto* trans = (*g)["translation"].as_table())
            {
                if (auto v = (*trans)["translate_enabled"].value<bool>())
                    global_translation_config_.translate_enabled = *v;
                if (auto v = (*trans)["auto_apply_changes"].value<bool>())
                    global_translation_config_.auto_apply_changes = *v;
                if (auto v = (*trans)["include_dialog_stream"].value<bool>())
                    global_translation_config_.include_dialog_stream = *v;
                if (auto v = (*trans)["include_corner_stream"].value<bool>())
                    global_translation_config_.include_corner_stream = *v;
                if (auto v = (*trans)["translation_backend"].value<int>())
                    global_translation_config_.translation_backend =
                        static_cast<TranslationConfig::TranslationBackend>(*v);
                if (auto v = (*trans)["target_lang"].value<std::string>())
                {
                    if (*v == "en-us")
                        global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::EN_US;
                    else if (*v == "zh-cn")
                        global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::ZH_CN;
                    else if (*v == "zh-tw")
                        global_translation_config_.target_lang_enum = TranslationConfig::TargetLang::ZH_TW;
                }
                if (auto v = (*trans)["custom_prompt"].value<std::string>())
                    safe_strncpy(global_translation_config_.custom_prompt.data(), v->c_str(),
                                 global_translation_config_.custom_prompt.size());
                if (auto* openai_tbl = (*trans)["openai"].as_table())
                {
                    if (auto v = (*openai_tbl)["base_url"].value<std::string>())
                        safe_strncpy(global_translation_config_.openai_base_url.data(), v->c_str(),
                                     global_translation_config_.openai_base_url.size());
                    if (auto v = (*openai_tbl)["model"].value<std::string>())
                        safe_strncpy(global_translation_config_.openai_model.data(), v->c_str(),
                                     global_translation_config_.openai_model.size());
                    if (auto v = (*openai_tbl)["api_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.openai_api_key.data(), v->c_str(),
                                     global_translation_config_.openai_api_key.size());
                }
                if (auto* google_tbl = (*trans)["google"].as_table())
                {
                    if (auto v = (*google_tbl)["api_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.google_api_key.data(), v->c_str(),
                                     global_translation_config_.google_api_key.size());
                }
                if (auto* qwen_tbl = (*trans)["qwen"].as_table())
                {
                    if (auto v = (*qwen_tbl)["api_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.qwen_api_key.data(), v->c_str(),
                                     global_translation_config_.qwen_api_key.size());
                    if (auto v = (*qwen_tbl)["model"].value<std::string>())
                        safe_strncpy(global_translation_config_.qwen_model.data(), v->c_str(),
                                     global_translation_config_.qwen_model.size());
                }
                if (auto* niutrans_tbl = (*trans)["niutrans"].as_table())
                {
                    if (auto v = (*niutrans_tbl)["api_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.niutrans_api_key.data(), v->c_str(),
                                     global_translation_config_.niutrans_api_key.size());
                }
                if (auto* zhipu_tbl = (*trans)["zhipu"].as_table())
                {
                    if (auto v = (*zhipu_tbl)["base_url"].value<std::string>())
                        safe_strncpy(global_translation_config_.zhipu_base_url.data(), v->c_str(),
                                     global_translation_config_.zhipu_base_url.size());
                    if (auto v = (*zhipu_tbl)["model"].value<std::string>())
                        safe_strncpy(global_translation_config_.zhipu_model.data(), v->c_str(),
                                     global_translation_config_.zhipu_model.size());
                    if (auto v = (*zhipu_tbl)["api_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.zhipu_api_key.data(), v->c_str(),
                                     global_translation_config_.zhipu_api_key.size());
                }
                if (auto* youdao_tbl = (*trans)["youdao"].as_table())
                {
                    if (auto v = (*youdao_tbl)["app_key"].value<std::string>())
                        safe_strncpy(global_translation_config_.youdao_app_key.data(), v->c_str(),
                                     global_translation_config_.youdao_app_key.size());
                    if (auto v = (*youdao_tbl)["app_secret"].value<std::string>())
                        safe_strncpy(global_translation_config_.youdao_app_secret.data(), v->c_str(),
                                     global_translation_config_.youdao_app_secret.size());
                    if (auto v = (*youdao_tbl)["mode"].value<int>())
                    {
                        if (*v == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                            global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::LargeModel;
                        else
                            global_translation_config_.youdao_mode = TranslationConfig::YoudaoMode::Text;
                    }
                }

                if (auto v = (*trans)["custom_prompt"].value<std::string>())
                    safe_strncpy(global_translation_config_.custom_prompt.data(), v->c_str(),
                                 global_translation_config_.custom_prompt.size());
                if (auto v = (*trans)["openai_base_url"].value<std::string>())
                    safe_strncpy(global_translation_config_.openai_base_url.data(), v->c_str(),
                                 global_translation_config_.openai_base_url.size());
                if (auto v = (*trans)["openai_model"].value<std::string>())
                    safe_strncpy(global_translation_config_.openai_model.data(), v->c_str(),
                                 global_translation_config_.openai_model.size());
                if (auto v = (*trans)["openai_api_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.openai_api_key.data(), v->c_str(),
                                 global_translation_config_.openai_api_key.size());
                if (auto v = (*trans)["google_api_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.google_api_key.data(), v->c_str(),
                                 global_translation_config_.google_api_key.size());
                if (auto v = (*trans)["qwen_api_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.qwen_api_key.data(), v->c_str(),
                                 global_translation_config_.qwen_api_key.size());
                if (auto v = (*trans)["qwen_model"].value<std::string>())
                    safe_strncpy(global_translation_config_.qwen_model.data(), v->c_str(),
                                 global_translation_config_.qwen_model.size());
                if (auto v = (*trans)["niutrans_api_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.niutrans_api_key.data(), v->c_str(),
                                 global_translation_config_.niutrans_api_key.size());
                if (auto v = (*trans)["zhipu_base_url"].value<std::string>())
                    safe_strncpy(global_translation_config_.zhipu_base_url.data(), v->c_str(),
                                 global_translation_config_.zhipu_base_url.size());
                if (auto v = (*trans)["zhipu_model"].value<std::string>())
                    safe_strncpy(global_translation_config_.zhipu_model.data(), v->c_str(),
                                 global_translation_config_.zhipu_model.size());
                if (auto v = (*trans)["zhipu_api_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.zhipu_api_key.data(), v->c_str(),
                                 global_translation_config_.zhipu_api_key.size());
                if (auto v = (*trans)["youdao_app_key"].value<std::string>())
                    safe_strncpy(global_translation_config_.youdao_app_key.data(), v->c_str(),
                                 global_translation_config_.youdao_app_key.size());
                if (auto v = (*trans)["youdao_app_secret"].value<std::string>())
                    safe_strncpy(global_translation_config_.youdao_app_secret.data(), v->c_str(),
                                 global_translation_config_.youdao_app_secret.size());
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

        // Parse [app.debug] section
        if (auto* dbg = root["app"]["debug"].as_table())
        {
            if (auto v = (*dbg)["profiling_level"].value<int>())
                setProfilingLevel(*v);
            if (auto v = (*dbg)["logging_level"].value<int>())
                setLoggingLevel(*v);
            if (auto v = (*dbg)["verbose"].value<bool>())
                setVerbose(*v);
            if (auto v = (*dbg)["compatibility_mode"].value<bool>())
                setCompatibilityMode(*v);
            if (auto v = (*dbg)["hook_wait_timeout_ms"].value<int>())
                setHookWaitTimeoutMs(*v);
        }

        if (auto* arr = root["dialogs"].as_array())
        {
            std::vector<std::pair<std::string, DialogStateManager>> dialog_configs;
            for (auto&& node : *arr)
            {
                if (!node.is_table())
                    continue;
                auto tbl = *node.as_table();
                DialogStateManager state;
                state.applyDefaults();
                if (state.translation_config().custom_prompt[0] == '\0')
                {
                    safe_strncpy(state.translation_config().custom_prompt.data(),
                                 i18n::get("dialog.settings.default_prompt"),
                                 state.translation_config().custom_prompt.size());
                }
                std::string name;
                if (StateSerializer::deserialize(tbl, state, name))
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
                    if (!dw)
                        continue;
                    dw->rename(dialog_configs[i].first.c_str());

                    dw->state() = dialog_configs[i].second;
                    dw->reinitializePlaceholder();

                    // Restore runtime-only state not persisted in config
                    dw->state().ui_state().window_size =
                        ImVec2(dw->state().ui_state().width, dw->state().ui_state().height);
                    dw->state().ui_state().pending_resize = true;
                    dw->state().ui_state().pending_reposition = true;
                    dw->state().ui_state().font = nullptr;
                    dw->state().ui_state().font_base_size = 0.0f;

                    // Rebind font pointers after state replacement
                    dw->refreshFontBinding();
                    dw->initTranslatorIfEnabled();

                    dw->setDefaultInstance(false);
                    if (default_dialog_enabled_ && i == 0)
                    {
                        registry_->markDialogAsDefault(*dw);
                    }
                }
            }
        }

        auto applyQuestConfig =
            [&](QuestWindow& quest_window, QuestStateManager& quest_state, const std::string& quest_name)
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
            quest_window.state().ui_state().window_size =
                ImVec2(quest_window.state().ui_state().width, quest_window.state().ui_state().height);
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
            if (quest_state.translation_config().custom_prompt[0] == '\0')
            {
                safe_strncpy(quest_state.translation_config().custom_prompt.data(),
                             i18n::get("dialog.settings.default_prompt"),
                             quest_state.translation_config().custom_prompt.size());
            }
            std::string quest_name;
            if (!StateSerializer::deserialize(quest_tbl, quest_state, quest_name))
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
                quest_window->setDefaultInstance(false);
                if (default_quest_enabled_ && index == 0)
                {
                    registry_->markQuestAsDefault(*quest_window);
                }
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
            if (default_quest_enabled_ && registry_->windowsByType(UIWindowType::Quest).empty())
            {
                registry_->createQuestWindow(true);
            }
        }

        auto processQuestHelperTable = [&](const toml::table& tbl, std::size_t index) {
            QuestHelperWindow* qh_window = nullptr;
            auto existing_quest_helpers = registry_->windowsByType(UIWindowType::QuestHelper);
            if (index < existing_quest_helpers.size())
            {
                qh_window = dynamic_cast<QuestHelperWindow*>(existing_quest_helpers[index]);
            }
            if (!qh_window)
            {
                qh_window = &registry_->createQuestHelperWindow(false);
            }
            std::string name;
            QuestHelperStateManager quest_helper_state;
            if (StateSerializer::deserialize(tbl, quest_helper_state, name))
            {
                qh_window->rename(name.c_str());
                WindowStateApplier::apply(*qh_window, quest_helper_state);
                qh_window->setDefaultInstance(false);
                if (default_quest_helper_enabled_ && index == 0)
                {
                    registry_->markQuestHelperAsDefault(*qh_window);
                }
            }
        };

        if (auto* quest_helper_arr = root["quest_helpers"].as_array())
        {
            std::size_t index = 0;
            for (auto&& node : *quest_helper_arr)
            {
                if (auto* qh_tbl = node.as_table())
                {
                    processQuestHelperTable(*qh_tbl, index);
                    ++index;
                }
            }
        }
        else
        {
            if (default_quest_helper_enabled_ && registry_->windowsByType(UIWindowType::QuestHelper).empty())
            {
                registry_->createQuestHelperWindow(true);
            }
        }

        guard.restore();
        enforceDefaultWindowStates();
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
            error_details =
                "Error at line " + std::to_string(pe.source().begin.line) + ": " + std::string(pe.description());
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

bool ConfigManager::loadAtStartup() { return loadAndApply(); }

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
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration, "Failed to reload configuration",
                                            last_error_.empty() ? std::string("See logs for details") : last_error_);
    }
}

void ConfigManager::setProfilingLevel(int level)
{
    // Clamp to valid range [0, 2]
    level = std::max(0, std::min(2, level));

    // Cap at build-time profiling level
#ifndef DQX_PROFILING_LEVEL
#define DQX_PROFILING_LEVEL 0
#endif
    profiling_level_ = std::min(level, DQX_PROFILING_LEVEL);

    // Update profiling logger (instance 2) severity based on profiling level
#if DQX_PROFILING_LEVEL >= 1
    if (auto* prof_logger = plog::get<profiling::kProfilingLogInstance>())
    {
        if (profiling_level_ == 0)
        {
            // Disable profiling logs completely
            prof_logger->setMaxSeverity(plog::none);
        }
        else
        {
            // Enable profiling logs (level 1=timer, level 2=tracy+timer both use debug severity)
            prof_logger->setMaxSeverity(plog::debug);
        }
    }
#endif

    // Note: Runtime profiling level control is limited by compile-time DQX_PROFILING_LEVEL
    // When DQX_PROFILING_LEVEL=0, profiling macros compile to no-ops regardless of runtime setting
}

void ConfigManager::setLoggingLevel(int level)
{
    // Validate severity range [0=none, 6=verbose]
    level = std::max(0, std::min(6, level));
    logging_level_ = level;

    auto severity = static_cast<plog::Severity>(level);

    // Apply logging level to all plog instances
    if (auto* logger = plog::get())
    {
        logger->setMaxSeverity(severity); // Instance 0: main logger (logs/run.log)
    }

    if (auto* diag_logger = plog::get<processing::Diagnostics::kLogInstance>())
    {
        diag_logger->setMaxSeverity(severity); // Instance 1: diagnostics logger (logs/dialog.log)
    }

    // Note: Profiling logger (instance 2) severity is controlled by profiling_level, not logging_level
    // See setProfilingLevel() for profiling logger control

    // Also update Diagnostics verbose mode (use debug or verbose level)
    processing::Diagnostics::SetVerbose(level >= 5); // 5=debug, 6=verbose
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
    if (g_cfg_mgr)
        return g_cfg_mgr->saveAll();
    return false;
}
