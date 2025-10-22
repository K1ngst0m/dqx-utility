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
#include "processing/Diagnostics.hpp"
#include "utils/Profile.hpp"
#include "platform/SingleInstanceGuard.hpp"
#include "utils/NativeMessageBox.hpp"

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <toml++/toml.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <clocale>
#endif

namespace
{

static void SDLCALL SDLLogBridge(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:
        PLOG_VERBOSE << "[SDL:" << category << "] " << message;
        break;
    case SDL_LOG_PRIORITY_DEBUG:
        PLOG_DEBUG << "[SDL:" << category << "] " << message;
        break;
    case SDL_LOG_PRIORITY_INFO:
        PLOG_INFO << "[SDL:" << category << "] " << message;
        break;
    case SDL_LOG_PRIORITY_WARN:
        PLOG_WARNING << "[SDL:" << category << "] " << message;
        break;
    case SDL_LOG_PRIORITY_ERROR:
        PLOG_ERROR << "[SDL:" << category << "] " << message;
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        PLOG_FATAL << "[SDL:" << category << "] " << message;
        break;
    default:
        PLOG_INFO << "[SDL:" << category << "] " << message;
        break;
    }
}

} // namespace

Application::Application(int argc, char** argv)
    : argc_(argc)
    , argv_(argv)
{
}

Application::~Application() { cleanup(); }

bool Application::initialize()
{
    PROFILE_SCOPE_FUNCTION();

    utils::CrashHandler::Initialize();
    i18n::init("en");

    if (!checkSingleInstance())
        return false;

    if (!initializeLogging())
        return false;

    initializeConsole();

    context_ = std::make_unique<AppContext>();
    if (!context_->initialize())
        return false;

    setupSDLLogging();
    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");

    setupManagers();
    initializeConfig();

    last_time_ = SDL_GetTicks();
    return true;
}

bool Application::initializeLogging()
{
    PROFILE_SCOPE_FUNCTION();

    std::error_code dir_ec;
    std::filesystem::create_directories("logs", dir_ec);
    if (dir_ec)
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization, "Unable to prepare log directory",
                                            dir_ec.message());
    }

    // Early read of append_logs setting from config.toml and set global flag
    bool append_logs = true; // Default to append mode
    try
    {
        auto cfg = toml::parse_file("config.toml");
        if (auto global = cfg["global"].as_table())
        {
            if (auto append = (*global)["append_logs"].value<bool>())
            {
                append_logs = *append;
            }
        }
    }
    catch (...)
    {
    }

    utils::ErrorReporter::InitializeLogFile("logs/error.log", append_logs);
    processing::Diagnostics::InitializeLogger(append_logs);

#if DQX_PROFILING_LEVEL >= 1
    profiling::InitializeProfilingLogger(append_logs);
#endif

    parseCommandLineArgs();

    if (!append_logs)
    {
        std::ofstream("logs/run.log", std::ios::trunc).close();
        std::ofstream("logs/quest.log", std::ios::trunc).close();
        std::ofstream("logs/network.log", std::ios::trunc).close();
    }

    static plog::RollingFileAppender<plog::TxtFormatter> file_appender("logs/run.log", 1024 * 1024 * 10, 3);
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;
    plog::init(plog::info, &file_appender);
    if (auto logger = plog::get())
        logger->addAppender(&console_appender);

    return true;
}

void Application::initializeConsole()
{
#ifdef _WIN32
    // Set Windows console to UTF-8 so non-ASCII text displays correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
        }
    }

    std::setlocale(LC_ALL, ".UTF-8");
#endif
}

void Application::setupSDLLogging()
{
    SDL_SetLogOutputFunction(SDLLogBridge, nullptr);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
}

void Application::setupManagers()
{
    PROFILE_SCOPE_FUNCTION();

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
    PROFILE_SCOPE_FUNCTION();

    config_->setRegistry(registry_.get());
    if (!config_->loadAtStartup())
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration, "Failed to load configuration",
                                            config_->lastError());
    }

    i18n::init(config_->getUILanguageCode());

    last_window_topmost_ = config_->getWindowAlwaysOnTop();
    context_->setWindowAlwaysOnTop(last_window_topmost_);

    config_->reconcileDefaultWindowStates();

    if (registry_->windowsByType(UIWindowType::Help).empty())
        registry_->createHelpWindow();

    config_->setAppMode(ConfigManager::AppMode::Normal);
    mode_manager_->ApplyModeSettings(ConfigManager::AppMode::Normal);
    mode_manager_->SetCurrentMode(ConfigManager::AppMode::Normal);
}

int Application::run()
{
    PROFILE_THREAD_NAME("MainThread");
    if (!initialize())
    {
        return -1;
    }
    mainLoop();
    return 0;
}

void Application::mainLoop()
{
    PROFILE_SCOPE_FUNCTION();

    while (running_)
    {
        float delta_time = calculateDeltaTime();
        processEvents();
        handleModeChanges();
        renderFrame(delta_time);
        handleQuitRequests();

        PROFILE_FRAME_STATS(frame_stats_);
    }
}

void Application::handleModeChanges()
{
    auto current_mode = config_->getAppMode();
    if (current_mode != mode_manager_->GetCurrentMode())
    {
        mode_manager_->HandleModeChange(mode_manager_->GetCurrentMode(), current_mode);
    }

    bool desired_topmost = config_->getWindowAlwaysOnTop();
    if (desired_topmost != last_window_topmost_)
    {
        context_->setWindowAlwaysOnTop(desired_topmost);
        last_window_topmost_ = desired_topmost;
    }
}

void Application::renderFrame(float deltaTime)
{
    PROFILE_SCOPE_FRAME(); 

    context_->beginFrame();

    setupMiniModeDockspace();
    renderWindows();
    handleUIRequests();

    if (show_settings_)
        settings_panel_->render(show_settings_);

    event_handler_->HandleTransparentAreaClick();
    context_->updateVignette(deltaTime);
    context_->renderVignette();
    context_->endFrame();

    utils::ErrorReporter::FlushPendingToHistory();
    PROFILE_FRAME_MARK();
}

void Application::handleQuitRequests()
{
    if (!quit_requested_)
        return;

    if (auto* dqxc = DQXClarityService_Get())
    {
        dqxc->shutdown();
        DQXClarityService_Set(nullptr);
    }

    saveConfig();
    running_ = false;
}

void Application::cleanup() { saveConfig(); }

bool Application::checkSingleInstance()
{
    instance_guard_ = SingleInstanceGuard::Acquire();
    if (instance_guard_)
        return true;

#ifdef _WIN32
    DWORD err = GetLastError();
    const char* msg_key =
        (err == ERROR_ALREADY_EXISTS) ? "error.native.single_instance_message" : "error.native.single_instance_generic";
    const char* detail_key = (err == ERROR_ALREADY_EXISTS) ? "error.native.single_instance_detail" :
                                                             "error.native.single_instance_generic_detail";

    utils::NativeMessageBox::ShowFatalError(i18n::get_str(msg_key), i18n::get_str(detail_key));
#else
    utils::NativeMessageBox::ShowFatalError(i18n::get_str("error.native.single_instance_message"),
                                            i18n::get_str("error.native.single_instance_detail"));
#endif
    return false;
}

void Application::parseCommandLineArgs()
{
    // Reserved for future CLI argument parsing
}

float Application::calculateDeltaTime()
{
    Uint64 current_time = SDL_GetTicks();
    float delta_time = (current_time - last_time_) / 1000.0f;
    last_time_ = current_time;
    return delta_time;
}

void Application::processEvents()
{
    SDL_Event event;

    if (SDL_WaitEventTimeout(&event, 16))
    {
        if (context_->processEvent(event))
            quit_requested_ = true;

        while (SDL_PollEvent(&event))
        {
            if (context_->processEvent(event))
                quit_requested_ = true;
        }
    }
}

void Application::setupMiniModeDockspace()
{
    auto current_mode = config_->getAppMode();
    ImGuiID dockspace_id = 0;

    if (current_mode == ConfigManager::AppMode::Mini)
    {
        dockspace_id = mini_manager_->SetupDockspace();
        DockState::SetDockspace(dockspace_id);
        mini_manager_->HandleAltDrag();
    }
    else
    {
        DockState::SetDockspace(0);
    }

    DockState::ConsumeReDock();
}

void Application::renderWindows()
{
    for (auto& window : registry_->windows())
        window->render();

    registry_->processRemovals();
}

void Application::handleUIRequests()
{
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
}

void Application::saveConfig()
{
    if (config_ && !config_->saveAll())
    {
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
                                          config_->lastError());
    }
}
