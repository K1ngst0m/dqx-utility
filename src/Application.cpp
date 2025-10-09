#include "Application.hpp"
#include "ui/AppContext.hpp"
#include "ui/FontManager.hpp"
#include "ui/GlobalSettingsPanel.hpp"
#include "ui/WindowRegistry.hpp"
#include "ui/ErrorDialog.hpp"
#include "ui/UIEventHandler.hpp"
#include "ui/MiniModeManager.hpp"
#include "ui/AppModeManager.hpp"
#include "config/ConfigManager.hpp"
#include "ui/DockState.hpp"
#include "utils/CrashHandler.hpp"
#include "ui/Localization.hpp"
#include "utils/ErrorReporter.hpp"
#include "services/DQXClarityService.hpp"
#include "platform/PlatformSetup.hpp"

#include <plog/Log.h>
#include <filesystem>

Application::Application() = default;
Application::~Application() { cleanup(); }

bool Application::initialize(int argc, char** argv)
{
    utils::CrashHandler::Initialize();

    std::filesystem::create_directories("logs");
    
    platform::PlatformSetup::InitializeConsole();

    context_ = std::make_unique<AppContext>();
    if (!context_->initialize())
        return false;

    platform::PlatformSetup::SetupSDLLogging();
    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");

    setupManagers();
    initializeConfig();
    
    last_time_ = SDL_GetTicks();
    return true;
}

void Application::setupManagers()
{
    font_manager_ = std::make_unique<FontManager>();
    registry_ = std::make_unique<WindowRegistry>(*font_manager_);

    config_ = std::make_unique<ConfigManager>();
    ConfigManager_Set(config_.get());
    
    event_handler_ = std::make_unique<ui::UIEventHandler>(*context_, *registry_);
    mini_manager_ = std::make_unique<ui::MiniModeManager>(*context_, *registry_);
    mode_manager_ = std::make_unique<ui::AppModeManager>(*context_, *registry_, *mini_manager_);
    
    settings_panel_ = std::make_unique<GlobalSettingsPanel>(*registry_);
    error_dialog_ = std::make_unique<ErrorDialog>();
}

void Application::initializeConfig()
{
    config_->setRegistry(registry_.get());
    config_->loadAtStartup();
    
    i18n::init(config_->getUILanguageCode());
    
    if (registry_->windowsByType(UIWindowType::Dialog).empty())
        registry_->createDialogWindow();
    
    config_->setAppMode(ConfigManager::AppMode::Normal);
    mode_manager_->ApplyModeSettings(ConfigManager::AppMode::Normal);
    mode_manager_->SetCurrentMode(ConfigManager::AppMode::Normal);
}

int Application::run()
{
    mainLoop();
    return 0;
}

void Application::mainLoop()
{
    while (running_)
    {
        Uint64 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time_) / 1000.0f;
        last_time_ = current_time;
        
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (context_->processEvent(event))
                quit_requested_ = true;
        }
        
        if (!running_)
            break;
        
        context_->updateVignette(delta_time);
        
        handleModeChanges();
        renderFrame();
        handleQuitRequests();
    }
}

void Application::handleModeChanges()
{
    auto current_mode = config_->getAppMode();
    if (current_mode != mode_manager_->GetCurrentMode())
    {
        mode_manager_->HandleModeChange(mode_manager_->GetCurrentMode(), current_mode);
    }
}

void Application::renderFrame()
{
    context_->beginFrame();
    
    ImGuiID dockspace_id = 0;
    auto current_mode = config_->getAppMode();
    if (current_mode == ConfigManager::AppMode::Mini)
        dockspace_id = mini_manager_->SetupDockspace();
    DockState::SetDockspace(dockspace_id);
    
    auto& io = ImGui::GetIO();
    
    for (auto& window : registry_->windows())
    {
        if (window)
            window->render();
    }
    
    registry_->processRemovals();
    
    if (current_mode == ConfigManager::AppMode::Mini)
        mini_manager_->HandleAltDrag();

    event_handler_->HandleTransparentAreaClick();
    event_handler_->RenderGlobalContextMenu(show_settings_, quit_requested_);
    
    if (config_->isGlobalSettingsRequested())
    {
        show_settings_ = true;
        config_->consumeGlobalSettingsRequest();
    }
    if (config_->isQuitRequested())
    {
        quit_requested_ = true;
        config_->consumeQuitRequest();
    }
    
    if (show_settings_)
        settings_panel_->render(show_settings_);
    
    if (utils::ErrorReporter::HasPendingErrors())
    {
        auto errors = utils::ErrorReporter::GetPendingErrors();
        error_dialog_->Show(errors);
    }
    
    if (error_dialog_->Render())
        quit_requested_ = true;
    
    DockState::ConsumeReDock();
    context_->renderVignette();
    context_->endFrame();
    SDL_Delay(16);
}

void Application::handleQuitRequests()
{
    if (quit_requested_)
    {
        if (auto* dqxc = DQXClarityService_Get())
            dqxc->stop();
        config_->saveAll();
        running_ = false;
    }
}

void Application::cleanup()
{
    if (config_)
        config_->saveAll();
}
