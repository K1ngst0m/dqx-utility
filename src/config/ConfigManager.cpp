#include "ConfigManager.hpp"

#include "../WindowRegistry.hpp"
#include "../DialogWindow.hpp"
#include "../state/DialogStateManager.hpp"
#include "../utils/ErrorReporter.hpp"

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
    t.insert("auto_scroll_to_new", state.ipc_config().auto_scroll_to_new);
    t.insert("portfile_path", std::string(state.ipc_config().portfile_path.data()));
    t.insert("translate_enabled", state.translation_config().translate_enabled);
    t.insert("auto_apply_changes", state.translation_config().auto_apply_changes);
    t.insert("translation_backend", static_cast<int>(state.translation_config().translation_backend));

    std::string target_lang;
    switch (state.translation_config().target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US: target_lang = "en-us"; break;
    case TranslationConfig::TargetLang::ZH_CN: target_lang = "zh-cn"; break;
    case TranslationConfig::TargetLang::ZH_TW: target_lang = "zh-tw"; break;
    }
    t.insert("target_lang", target_lang);

    t.insert("openai_base_url", std::string(state.translation_config().openai_base_url.data()));
    t.insert("openai_model", std::string(state.translation_config().openai_model.data()));
    t.insert("openai_api_key", std::string(state.translation_config().openai_api_key.data()));
    t.insert("google_api_key", std::string(state.translation_config().google_api_key.data()));

    // GUI properties
    t.insert("width", state.ui_state().width);
    t.insert("height", state.ui_state().height);
    t.insert("pos_x", state.ui_state().window_pos.x);
    t.insert("pos_y", state.ui_state().window_pos.y);
    t.insert("padding_x", state.ui_state().padding.x);
    t.insert("padding_y", state.ui_state().padding.y);
    t.insert("rounding", state.ui_state().rounding);
    t.insert("border_thickness", state.ui_state().border_thickness);
    t.insert("background_alpha", state.ui_state().background_alpha);
    t.insert("font_size", state.ui_state().font_size);
    t.insert("font_path", std::string(state.ui_state().font_path.data()));

    return t;
}

static bool tomlToDialogState(const toml::table& t, DialogStateManager& state, std::string& name)
{
    auto name_val = t["name"].value<std::string>();
    if (!name_val) return false;
    name = *name_val;

    if (auto v = t["auto_scroll_to_new"].value<bool>()) state.ipc_config().auto_scroll_to_new = *v;
    if (auto v = t["portfile_path"].value<std::string>())
        std::snprintf(state.ipc_config().portfile_path.data(), state.ipc_config().portfile_path.size(), "%s", v->c_str());
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
    if (auto v = t["font_path"].value<std::string>())
        std::snprintf(state.ui_state().font_path.data(), state.ui_state().font_path.size(), "%s", v->c_str());

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
        return false;
    }

    toml::table root;
    toml::table global;
    global.insert("ui_scale", ui_scale_);
    global.insert("append_logs", append_logs_);
    global.insert("borderless_windows", borderless_windows_);
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
        }

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
                    
                    // Apply loaded config (which already has defaults applied)
                    dw->state() = dialog_configs[i].second;
                    
                    // Restore runtime-only state not persisted in config
                    dw->state().ui_state().window_size = ImVec2(dw->state().ui_state().width, dw->state().ui_state().height);
                    dw->state().ui_state().pending_resize = true;
                    dw->state().ui_state().pending_reposition = true;
                    dw->state().ui_state().font = nullptr;
                    dw->state().ui_state().font_base_size = dw->state().ui_state().font_size;
                    
                    // Rebind font pointers after state replacement
                    dw->refreshFontBinding();
                    dw->initTranslatorIfEnabled();
                    dw->autoConnectIPC();
                }
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
}

ConfigManager* ConfigManager_Get() { return g_cfg_mgr; }
void ConfigManager_Set(ConfigManager* mgr) { g_cfg_mgr = mgr; }

bool ConfigManager_SaveAll()
{
    if (g_cfg_mgr) return g_cfg_mgr->saveAll();
    return false;
}
