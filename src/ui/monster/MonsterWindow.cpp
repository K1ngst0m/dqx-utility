#include "MonsterWindow.hpp"
#include "../FontManager.hpp"
#include "../GlobalStateManager.hpp"
#include "../UIHelper.hpp"
#include "../common/AppearanceSettingsPanel.hpp"
#include "../DockState.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../monster/MonsterManager.hpp"
#include "../../monster/MonsterInfo.hpp"
#include "../../processing/GlossaryManager.hpp"
#include "../../translate/ITranslator.hpp"
#include "../../translate/TranslationConfig.hpp"

#include <imgui.h>
#include <plog/Log.h>

MonsterWindow::MonsterWindow(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config,
                             MonsterManager& monster_manager, const std::string& monster_id, const std::string& name)
    : font_manager_(font_manager)
    , global_state_(global_state)
    , config_(config)
    , monster_manager_(monster_manager)
    , monster_id_(monster_id)
    , name_(name)
    , window_label_(name + "##Monster_" + monster_id)
{
    static int monster_window_counter = 0;
    ++monster_window_counter;
    settings_id_suffix_ = "MonsterSettings_" + std::to_string(monster_window_counter);
    settings_window_label_ = name_ + " Settings##" + settings_id_suffix_;
    
    state_.applyDefaults();
    appearance_panel_ = std::make_unique<AppearanceSettingsPanel>(state_);
    
    // Initialize translation session
    session_.setCapacity(5000);
    session_.enableCache(true);
    
    font_manager_.registerDialog(state_.ui_state());
    refreshFontBinding();
}

MonsterWindow::~MonsterWindow()
{
    font_manager_.unregisterDialog(state_.ui_state());
}

void MonsterWindow::rename(const char* new_name)
{
    if (!new_name)
        return;
    name_ = new_name;
    window_label_ = std::string(new_name) + "##Monster_" + monster_id_;
    settings_window_label_ = std::string(new_name) + " Settings##" + settings_id_suffix_;
}

void MonsterWindow::render()
{
    refreshFontBinding();
    applyPendingResizeFlags();
    pollTranslations();
    
    if (want_focus_)
    {
        ImGui::SetNextWindowFocus();
        want_focus_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(state_.ui.width, state_.ui.height), ImGuiCond_FirstUseEver);
    
    float fade_alpha = state_.ui.current_alpha_multiplier;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade_alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, state_.ui.padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, state_.ui.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, state_.ui.border_enabled ? state_.ui.border_thickness : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, state_.ui.background_alpha * fade_alpha));
    
    bool window_open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(window_label_.c_str(), &window_open, flags))
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        ImGui::End();
        return;
    }
    
    state_.ui.is_docked = ImGui::IsWindowDocked();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();
    state_.ui.window_pos = win_pos;
    state_.ui.window_size = win_size;
    
    if (state_.ui.vignette_thickness > 0.0f)
    {
        ui::RenderVignette(win_pos, win_size, state_.ui.vignette_thickness, state_.ui.rounding, fade_alpha);
    }
    
    ImFont* active_font = state_.ui.font;
    float font_scale = 1.0f;
    if (active_font && state_.ui.font_base_size > 0.0f)
    {
        font_scale = std::max(0.3f, state_.ui.font_size / state_.ui.font_base_size);
    }
    if (active_font)
    {
        ImGui::PushFont(active_font);
        ImGui::SetWindowFontScale(font_scale);
    }

    auto monster_info = monster_manager_.findMonsterById(monster_id_);
    
    if (!monster_info.has_value())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", i18n::format("monster.not_found", {{"id", monster_id_}}).c_str());
        if (active_font)
        {
            ImGui::PopFont();
            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        return;
    }

    // Title (with translation and tooltip)
    std::string translated_name = getTranslatedText(monster_info->name);
    renderTextWithTooltip(translated_name.c_str(), monster_info->name.c_str());
    
    // Category with glossary translation
    static processing::GlossaryManager glossary_manager;
    static bool glossary_initialized = false;
    if (!glossary_initialized)
    {
        glossary_manager.initialize();
        glossary_initialized = true;
    }
    
    std::string target_lang = "zh-CN";
    std::optional<std::string> translated_category = glossary_manager.lookup(monster_info->category, target_lang);
    
    ImGui::TextDisabled("%s: ", i18n::get("monster.ui.category"));
    ImGui::SameLine(0.0f, 0.0f);
    if (translated_category.has_value())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        renderTextWithTooltip(translated_category.value().c_str(), monster_info->category.c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("%s", monster_info->category.c_str());
    }
    
    ui::DrawDefaultSeparator();

    if (ImGui::CollapsingHeader((std::string(i18n::get("monster.sections.stats")) + "##" + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderStatsSection(monster_info->stats);
    }

    if (ImGui::CollapsingHeader((std::string(i18n::get("monster.sections.resistances")) + "##" + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderResistancesSection(monster_info->resistances);
    }

    if (!monster_info->locations.empty())
    {
        if (ImGui::CollapsingHeader((std::string(i18n::get("monster.sections.locations")) + "##" + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            renderLocationsSection(monster_info->locations);
        }
    }

    if (ImGui::CollapsingHeader((std::string(i18n::get("monster.sections.drops")) + "##" + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderDropsSection(monster_info->drops);
    }
    
    if (active_font)
    {
        ImGui::PopFont();
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    
    if (!window_open)
    {
        should_be_removed_ = true;
    }
    
    renderContextMenu();
    renderSettingsWindow();
}

void MonsterWindow::renderSettings()
{
    show_settings_window_ = true;
}

void MonsterWindow::renderStatsSection(const monster::MonsterStats& stats)
{
    // First block: 4 columns
    if (ImGui::BeginTable((std::string("StatsTableA##") + monster_id_).c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn(i18n::get("monster.stats.exp"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.gold"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.training"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.weak_lv"));
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.exp.has_value() ? std::to_string(stats.exp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.gold.has_value() ? std::to_string(stats.gold.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.training.has_value() ? std::to_string(stats.training.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.weak_level.has_value() ? std::to_string(stats.weak_level.value()).c_str() : "-");

        ImGui::EndTable();
    }

    // Second block: 5 columns
    if (ImGui::BeginTable((std::string("StatsTableB##") + monster_id_).c_str(), 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn(i18n::get("monster.stats.hp"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.mp"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.attack"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.defense"));
        ImGui::TableSetupColumn(i18n::get("monster.stats.crystal_lv"));
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.hp.has_value() ? std::to_string(stats.hp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.mp.has_value() ? std::to_string(stats.mp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.attack.has_value() ? std::to_string(stats.attack.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.defense.has_value() ? std::to_string(stats.defense.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.crystal_level.has_value() ? stats.crystal_level.value().c_str() : "-");

        ImGui::EndTable();
    }
}

void MonsterWindow::renderContextMenu()
{
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    bool within_window = ImGui::IsMousePosValid(&mouse_pos) &&
                         ImGui::IsMouseHoveringRect(
                             state_.ui.window_pos,
                             ImVec2(state_.ui.window_pos.x + state_.ui.window_size.x,
                                    state_.ui.window_pos.y + state_.ui.window_size.y),
                             false);

    std::string popup_id = "MonsterContextMenu###" + monster_id_;
    if (within_window && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup(popup_id.c_str());

    bool is_docked = state_.ui.is_docked;
    if (ImGui::BeginPopup(popup_id.c_str()))
    {
        if (ImGui::MenuItem(ui::LocalizedOrFallback("window.context.settings", "Settings...").c_str()))
            show_settings_window_ = !show_settings_window_;

        ImGui::Separator();

        float min_font = std::max(8.0f, state_.ui.font_base_size * 0.5f);
        float max_font = state_.ui.font_base_size * 2.5f;
        bool can_increase = state_.ui.font_size < max_font;
        bool can_decrease = state_.ui.font_size > min_font;

        if (ImGui::MenuItem(ui::LocalizedOrFallback("dialog.context_menu.increase_font", "Increase Font").c_str(), nullptr, false, can_increase))
            state_.ui.font_size = std::min(state_.ui.font_size + 2.0f, max_font);

        if (ImGui::MenuItem(ui::LocalizedOrFallback("dialog.context_menu.decrease_font", "Decrease Font").c_str(), nullptr, false, can_decrease))
            state_.ui.font_size = std::max(state_.ui.font_size - 2.0f, min_font);

        ImGui::Separator();

        bool can_remove = !is_docked;
        if (ImGui::MenuItem(ui::LocalizedOrFallback("common.remove", "Remove").c_str(), nullptr, false, can_remove))
            should_be_removed_ = true;

        if (is_docked)
        {
            ImGui::Separator();
            if (ImGui::MenuItem(ui::LocalizedOrFallback("menu.global_settings", "Global Settings...").c_str()))
                config_.requestShowGlobalSettings();

            if (ImGui::BeginMenu(ui::LocalizedOrFallback("menu.app_mode", "App Mode").c_str()))
            {
                auto& gs = global_state_;
                auto mode = gs.appMode();
                bool sel_normal = (mode == GlobalStateManager::AppMode::Normal);
                bool sel_borderless = (mode == GlobalStateManager::AppMode::Borderless);
                bool sel_mini = (mode == GlobalStateManager::AppMode::Mini);
                if (ImGui::MenuItem(ui::LocalizedOrFallback("settings.app_mode.items.normal", "Normal").c_str(), nullptr, sel_normal))
                    gs.setAppMode(GlobalStateManager::AppMode::Normal);
                if (ImGui::MenuItem(ui::LocalizedOrFallback("settings.app_mode.items.borderless", "Borderless").c_str(), nullptr, sel_borderless))
                    gs.setAppMode(GlobalStateManager::AppMode::Borderless);
                if (ImGui::MenuItem(ui::LocalizedOrFallback("settings.app_mode.items.mini", "Mini").c_str(), nullptr, sel_mini))
                    gs.setAppMode(GlobalStateManager::AppMode::Mini);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem(ui::LocalizedOrFallback("menu.quit", "Quit").c_str()))
                config_.requestQuit();
        }

        ImGui::EndPopup();
    }
}

void MonsterWindow::renderSettingsWindow()
{
    if (!show_settings_window_)
        return;
    
    ImGui::SetNextWindowSize(ImVec2(440.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    
    if (ImGui::Begin(settings_window_label_.c_str(), &show_settings_window_))
    {
        if (ImGui::CollapsingHeader(ui::LocalizedOrFallback("settings.appearance", "Appearance").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto changes = appearance_panel_->render();
            
            if (changes.width_changed)
            {
                state_.ui.window_size.x = state_.ui.width;
                state_.ui.pending_resize = true;
            }
            
            if (changes.height_changed)
            {
                state_.ui.window_size.y = state_.ui.height;
                state_.ui.pending_resize = true;
            }
            
            if (changes.font_changed)
            {
                refreshFontBinding();
            }
        }
    }
    ImGui::End();
}

void MonsterWindow::refreshFontBinding()
{
    font_manager_.ensureFont(state_.ui);
}

void MonsterWindow::applyPendingResizeFlags()
{
    if (state_.ui.pending_resize)
    {
        state_.ui.window_size = ImVec2(state_.ui.width, state_.ui.height);
        ImGui::SetNextWindowSize(state_.ui.window_size, ImGuiCond_Always);
        state_.ui.pending_resize = false;
    }
    
    if (state_.ui.pending_reposition)
    {
        ImGui::SetNextWindowPos(state_.ui.window_pos, ImGuiCond_Always);
        state_.ui.pending_reposition = false;
    }
}

void MonsterWindow::renderResistancesSection(const monster::MonsterResistances& resistances)
{
    if (ImGui::BeginTable((std::string("ResistancesTable##") + monster_id_).c_str(), 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn(i18n::get("monster.resistances.fire"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.ice"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.wind"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.thunder"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.earth"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.dark"));
        ImGui::TableSetupColumn(i18n::get("monster.resistances.light"));
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        
        auto renderResistance = [](const std::optional<double>& value) {
            if (value.has_value())
            {
                double v = value.value();
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (v < 1.0)
                    color = ImVec4(0.8f, 0.3f, 0.3f, 1.0f); // Weak (red)
                else if (v > 1.0)
                    color = ImVec4(0.3f, 0.8f, 0.3f, 1.0f); // Resistant (green)
                
                ImGui::TextColored(color, "%.1f", v);
            }
            else
            {
                ImGui::Text("-");
            }
        };

        ImGui::TableNextColumn();
        renderResistance(resistances.fire);
        ImGui::TableNextColumn();
        renderResistance(resistances.ice);
        ImGui::TableNextColumn();
        renderResistance(resistances.wind);
        ImGui::TableNextColumn();
        renderResistance(resistances.thunder);
        ImGui::TableNextColumn();
        renderResistance(resistances.earth);
        ImGui::TableNextColumn();
        renderResistance(resistances.dark);
        ImGui::TableNextColumn();
        renderResistance(resistances.light);

        ImGui::EndTable();
    }
}

void MonsterWindow::renderLocationsSection(const std::vector<monster::MonsterLocation>& locations)
{
    // Get shared glossary manager
    static processing::GlossaryManager glossary_manager;
    static bool initialized = false;
    if (!initialized)
    {
        glossary_manager.initialize();
        initialized = true;
    }
    
    // Get target language from config (assume zh-CN for now)
    std::string target_lang = "zh-CN";
    
    for (size_t i = 0; i < locations.size(); ++i)
    {
        const auto& loc = locations[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Bullet();
        
        // Try exact match first
        std::optional<std::string> translated = glossary_manager.lookup(loc.area, target_lang);
        
        // If exact match fails, try fuzzy matching
        if (!translated.has_value())
        {
            auto fuzzy_results = glossary_manager.fuzzyLookup(loc.area, target_lang, 0.8);
            if (!fuzzy_results.empty())
            {
                // Use the best fuzzy match
                translated = std::get<1>(fuzzy_results[0]);
            }
        }
        
        // Display translated text with tooltip showing original
        if (translated.has_value())
        {
            // Build display text with notes if available
            std::string display_text = translated.value();
            if (loc.notes.has_value() && !loc.notes.value().empty())
            {
                display_text += " (" + loc.notes.value() + ")";
            }
            
            renderTextWithTooltip(display_text.c_str(), loc.area.c_str());
        }
        else
        {
            // No translation found, display original only
            if (loc.notes.has_value() && !loc.notes.value().empty())
            {
                ImGui::Text("%s (%s)", loc.area.c_str(), loc.notes.value().c_str());
            }
            else
            {
                ImGui::Text("%s", loc.area.c_str());
            }
        }
        
        ImGui::PopID();
    }
}

void MonsterWindow::renderDropsSection(const monster::MonsterDrops& drops)
{
    if (ImGui::BeginTable((std::string("DropsTable##") + monster_id_).c_str(), 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthStretch);

        // Normal drops
        if (!drops.normal.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.3f, 1.0f), "%s", i18n::get("monster.drops.normal"));
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.normal.size(); ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(", ");
                    ImGui::SameLine();
                }
                std::string translated_item = getTranslatedText(drops.normal[i]);
                renderTextWithTooltip(translated_item.c_str(), drops.normal[i].c_str());
                if (i < drops.normal.size() - 1)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                }
            }
        }

        // Rare drops
        if (!drops.rare.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.3f, 0.8f, 1.0f), "%s", i18n::get("monster.drops.rare"));
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.rare.size(); ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(", ");
                    ImGui::SameLine();
                }
                std::string translated_item = getTranslatedText(drops.rare[i]);
                renderTextWithTooltip(translated_item.c_str(), drops.rare[i].c_str());
                if (i < drops.rare.size() - 1)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                }
            }
        }

        // Orbs
        if (!drops.orbs.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", i18n::get("monster.drops.orbs"));
            ImGui::TableNextColumn();
            std::string items_text;
            for (size_t i = 0; i < drops.orbs.size(); ++i)
            {
                const auto& orb = drops.orbs[i];
                if (i > 0) items_text += ", ";
                items_text += "[" + orb.orb_type + "]";
                if (!orb.effect.empty())
                {
                    items_text += " " + orb.effect;
                }
            }
            ImGui::Text("%s", items_text.c_str());
        }

        // White treasure
        if (!drops.white_treasure.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", i18n::get("monster.drops.white_treasure"));
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.white_treasure.size(); ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(", ");
                    ImGui::SameLine();
                }
                std::string translated_item = getTranslatedText(drops.white_treasure[i]);
                renderTextWithTooltip(translated_item.c_str(), drops.white_treasure[i].c_str());
                if (i < drops.white_treasure.size() - 1)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                }
            }
        }

        ImGui::EndTable();
    }
}

void MonsterWindow::initTranslatorIfEnabled()
{
    const TranslationConfig& config = global_state_.translationConfig();
    if (!config.translate_enabled)
    {
        if (translator_)
        {
            translator_->shutdown();
            translator_.reset();
        }
        translator_initialized_ = false;
        return;
    }
    
    translate::BackendConfig cfg = translate::BackendConfig::from(config);
    
    bool same_backend = translator_initialized_ && translator_ && cfg.backend == cached_backend_;
    bool same_config = same_backend && cfg.base_url == cached_config_.base_url && cfg.model == cached_config_.model &&
                       cfg.api_key == cached_config_.api_key && cfg.api_secret == cached_config_.api_secret &&
                       cfg.target_lang == cached_config_.target_lang;
    
    if (same_config && translator_ && translator_->isReady())
    {
        return;
    }
    
    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }
    
    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg))
    {
        if (translator_)
        {
            translator_->shutdown();
            translator_.reset();
        }
        translator_initialized_ = false;
        cached_backend_ = cfg.backend;
        cached_config_ = cfg;
        return;
    }
    
    if (!translator_->isReady())
    {
        translator_->shutdown();
        translator_.reset();
        translator_initialized_ = false;
        cached_backend_ = cfg.backend;
        cached_config_ = cfg;
        return;
    }
    
    translator_initialized_ = true;
    cached_backend_ = cfg.backend;
    cached_config_ = cfg;
}

void MonsterWindow::pollTranslations()
{
    if (!translator_initialized_ || !translator_)
        return;
    
    std::vector<translate::Completed> done;
    if (!translator_->drain(done) || done.empty())
        return;
    
    std::vector<TranslateSession::CompletedEvent> events;
    session_.onCompleted(done, events);
    
    for (const auto& ev : events)
    {
        if (!ev.failed)
        {
            translation_cache_[ev.original_text] = ev.text;
        }
    }
}

std::string MonsterWindow::getTranslatedText(const std::string& original_text)
{
    // Initialize translator if not done yet
    if (!translator_initialized_)
    {
        initTranslatorIfEnabled();
    }
    
    // Check cache first
    auto it = translation_cache_.find(original_text);
    if (it != translation_cache_.end())
    {
        return it->second;
    }
    
    // If translation not enabled or translator not ready, return original
    const TranslationConfig& config = global_state_.translationConfig();
    if (!config.translate_enabled || !translator_initialized_ || !translator_)
    {
        return original_text;
    }
    
    // Submit for translation
    auto submit = session_.submit(original_text, config.translation_backend,
                                  config.target_lang_enum, translator_.get());
    
    if (submit.kind == TranslateSession::SubmitKind::Cached)
    {
        translation_cache_[original_text] = submit.text;
        return submit.text;
    }
    
    // Return original text while waiting for translation
    return original_text;
}

void MonsterWindow::renderTextWithTooltip(const char* text, const char* tooltip)
{
    ImGui::TextUnformatted(text);
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
}
