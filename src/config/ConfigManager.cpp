#include "ConfigManager.hpp"

#include "../WindowRegistry.hpp"
#include "../DialogWindow.hpp"
#include "../DialogState.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <imgui.h>

namespace fs = std::filesystem;

static ConfigManager* g_cfg_mgr = nullptr;

// Snapshot of all dialog window states for config save/load
struct DialogsSnapshot
{
    struct Entry
    {
        std::string name;
        bool auto_scroll = true;
        std::string portfile_path;
        bool translate_enabled = false;
        int translation_backend = 0; // 0=OpenAI, 1=Google
        std::string target_lang; // "en-us" | "zh-cn" | "zh-tw"
        std::string base_url;
        std::string model;
        std::string api_key;
        std::string google_api_key;
        // GUI properties
        float width = 580.0f;
        float height = 220.0f;
        float padding_x = 24.0f;
        float padding_y = 18.0f;
        float rounding = 16.0f;
        float border_thickness = 2.0f;
        float background_alpha = 0.78f;
        float font_size = 28.0f;
        std::string font_path;
    };
    std::vector<Entry> entries;
};

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

static DialogsSnapshot snapshot_from_registry(WindowRegistry* registry)
{
    DialogsSnapshot snap;
    if (!registry) return snap;
    auto windows = registry->windowsByType(UIWindowType::Dialog);
    for (auto* w : windows)
    {
        auto* dw = dynamic_cast<DialogWindow*>(w);
        if (!dw) continue;
        DialogsSnapshot::Entry e;
        e.name = dw->displayName();
        DialogState& st = dw->state();
        e.auto_scroll = st.auto_scroll_to_new;
        e.portfile_path = st.portfile_path.data();
        e.translate_enabled = st.translate_enabled;
        e.translation_backend = static_cast<int>(st.translation_backend);
        switch (st.target_lang_enum)
        {
        case DialogState::TargetLang::EN_US: e.target_lang = "en-us"; break;
        case DialogState::TargetLang::ZH_CN: e.target_lang = "zh-cn"; break;
        case DialogState::TargetLang::ZH_TW: e.target_lang = "zh-tw"; break;
        }
        e.base_url = st.openai_base_url.data();
        e.model = st.openai_model.data();
        e.api_key = st.openai_api_key.data();
        e.google_api_key = st.google_api_key.data();
        // GUI properties
        e.width = st.width;
        e.height = st.height;
        e.padding_x = st.padding.x;
        e.padding_y = st.padding.y;
        e.rounding = st.rounding;
        e.border_thickness = st.border_thickness;
        e.background_alpha = st.background_alpha;
        e.font_size = st.font_size;
        e.font_path = st.font_path.data();
        snap.entries.push_back(std::move(e));
    }
    return snap;
}

bool ConfigManager::saveAll()
{
    last_error_.clear();
    if (!registry_)
    {
        last_error_ = "No registry assigned";
        return false;
    }
    auto snap = snapshot_from_registry(registry_);
    toml::table root;
    // global section
    toml::table global;
    global.insert("ui_scale", ui_scale_);
    root.insert("global", std::move(global));

    toml::array arr;
    for (const auto& e : snap.entries)
    {
        toml::table t;
        t.insert("name", e.name);
        t.insert("auto_scroll_to_new", e.auto_scroll);
        t.insert("portfile_path", e.portfile_path);
        t.insert("translate_enabled", e.translate_enabled);
        t.insert("translation_backend", e.translation_backend);
        t.insert("target_lang", e.target_lang);
        t.insert("openai_base_url", e.base_url);
        t.insert("openai_model", e.model);
        t.insert("openai_api_key", e.api_key);
        t.insert("google_api_key", e.google_api_key);
        // GUI properties
        t.insert("width", e.width);
        t.insert("height", e.height);
        t.insert("padding_x", e.padding_x);
        t.insert("padding_y", e.padding_y);
        t.insert("rounding", e.rounding);
        t.insert("border_thickness", e.border_thickness);
        t.insert("background_alpha", e.background_alpha);
        t.insert("font_size", e.font_size);
        t.insert("font_path", e.font_path);
        arr.push_back(std::move(t));
    }
    root.insert("dialogs", std::move(arr));

    // atomic write
    std::string tmp = config_path_ + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs)
    {
        last_error_ = "Failed to open temp file for writing";
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
        return false;
    }
    last_mtime_ = file_mtime_ms(config_path_);
    PLOG_INFO << "Saved config to " << config_path_;
    return true;
}

static bool apply_entry_to_dialog(const DialogsSnapshot::Entry& e, DialogWindow* dw)
{
    if (!dw) return false;
    dw->rename(e.name.c_str());
    DialogState& st = dw->state();
    st.auto_scroll_to_new = e.auto_scroll;
    std::snprintf(st.portfile_path.data(), st.portfile_path.size(), "%s", e.portfile_path.c_str());
    st.translate_enabled = e.translate_enabled;
    st.translation_backend = static_cast<DialogState::TranslationBackend>(e.translation_backend);
    if (e.target_lang == "en-us") st.target_lang_enum = DialogState::TargetLang::EN_US;
    else if (e.target_lang == "zh-cn") st.target_lang_enum = DialogState::TargetLang::ZH_CN;
    else if (e.target_lang == "zh-tw") st.target_lang_enum = DialogState::TargetLang::ZH_TW;
    std::snprintf(st.openai_base_url.data(), st.openai_base_url.size(), "%s", e.base_url.c_str());
    std::snprintf(st.openai_model.data(), st.openai_model.size(), "%s", e.model.c_str());
    std::snprintf(st.openai_api_key.data(), st.openai_api_key.size(), "%s", e.api_key.c_str());
    std::snprintf(st.google_api_key.data(), st.google_api_key.size(), "%s", e.google_api_key.c_str());
    // GUI properties
    st.width = e.width;
    st.height = e.height;
    st.padding.x = e.padding_x;
    st.padding.y = e.padding_y;
    st.rounding = e.rounding;
    st.border_thickness = e.border_thickness;
    st.background_alpha = e.background_alpha;
    st.font_size = e.font_size;
    std::snprintf(st.font_path.data(), st.font_path.size(), "%s", e.font_path.c_str());
    // Initialize translator if enabled
    dw->initTranslatorIfEnabled();
    // Auto-connect IPC if portfile path is configured
    dw->autoConnectIPC();
    return true;
}

bool ConfigManager::applyDialogs(const DialogsSnapshot& snap)
{
    if (!registry_) return false;
    // ensure count
    auto windows = registry_->windowsByType(UIWindowType::Dialog);
    int have = static_cast<int>(windows.size());
    int want = static_cast<int>(snap.entries.size());
    if (want > have)
    {
        for (int i = 0; i < want - have; ++i)
            registry_->createDialogWindow();
        windows = registry_->windowsByType(UIWindowType::Dialog);
    }
    else if (want < have)
    {
        // remove extra from end
        for (int i = have - 1; i >= want; --i)
        {
            registry_->removeWindow(windows[i]);
        }
        windows = registry_->windowsByType(UIWindowType::Dialog);
    }

    // apply by index
    int n = std::min(static_cast<int>(windows.size()), want);
    for (int i = 0; i < n; ++i)
    {
        auto* dw = dynamic_cast<DialogWindow*>(windows[i]);
        if (!dw) continue;
        apply_entry_to_dialog(snap.entries[i], dw);
    }
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
        }

        DialogsSnapshot snap;
        if (auto* arr = root["dialogs"].as_array())
        {
            for (auto&& node : *arr)
            {
                if (!node.is_table()) continue;
                auto tbl = *node.as_table();
                DialogsSnapshot::Entry e;
                if (auto v = tbl["name"].value<std::string>()) e.name = *v;
                if (auto v = tbl["auto_scroll_to_new"].value<bool>()) e.auto_scroll = *v;
                if (auto v = tbl["portfile_path"].value<std::string>()) e.portfile_path = *v;
                if (auto v = tbl["translate_enabled"].value<bool>()) e.translate_enabled = *v;
                if (auto v = tbl["translation_backend"].value<int>()) e.translation_backend = *v;
                if (auto v = tbl["target_lang"].value<std::string>()) e.target_lang = *v;
                if (auto v = tbl["openai_base_url"].value<std::string>()) e.base_url = *v;
                if (auto v = tbl["openai_model"].value<std::string>()) e.model = *v;
                if (auto v = tbl["openai_api_key"].value<std::string>()) e.api_key = *v;
                if (auto v = tbl["google_api_key"].value<std::string>()) e.google_api_key = *v;
                // GUI properties
                if (auto v = tbl["width"].value<float>()) e.width = *v;
                if (auto v = tbl["height"].value<float>()) e.height = *v;
                if (auto v = tbl["padding_x"].value<float>()) e.padding_x = *v;
                if (auto v = tbl["padding_y"].value<float>()) e.padding_y = *v;
                if (auto v = tbl["rounding"].value<float>()) e.rounding = *v;
                if (auto v = tbl["border_thickness"].value<float>()) e.border_thickness = *v;
                if (auto v = tbl["background_alpha"].value<float>()) e.background_alpha = *v;
                if (auto v = tbl["font_size"].value<float>()) e.font_size = *v;
                if (auto v = tbl["font_path"].value<std::string>()) e.font_path = *v;
                if (!e.name.empty())
                    snap.entries.push_back(std::move(e));
            }
        }
        if (!snap.entries.empty())
        {
            applyDialogs(snap);
        }
        return true;
    }
    catch (const toml::parse_error& pe)
    {
        last_error_ = std::string("config parse error: ") + std::string(pe.description());
        PLOG_WARNING << last_error_;
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
