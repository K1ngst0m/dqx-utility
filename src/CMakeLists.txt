include(PlatformWindows)

add_library(dqxu_core STATIC)

target_sources(dqxu_core
  PRIVATE
    app/Application.cpp
    app/Application.hpp
)

target_sources(dqxu_core
  PRIVATE
    ui/AppContext.cpp
    ui/AppContext.hpp
    ui/AppModeManager.cpp
    ui/AppModeManager.hpp
    ui/DockState.cpp
    ui/DockState.hpp
    ui/ErrorDialog.cpp
    ui/ErrorDialog.hpp
    ui/FontManager.cpp
    ui/FontManager.hpp
    ui/GlobalSettingsPanel.cpp
    ui/GlobalSettingsPanel.hpp
    ui/Localization.cpp
    ui/Localization.hpp
    ui/MiniModeManager.cpp
    ui/MiniModeManager.hpp
    ui/UIEventHandler.cpp
    ui/UIEventHandler.hpp
    ui/UIHelper.hpp
    ui/UITheme.cpp
    ui/UITheme.hpp
    ui/WindowAnimator.cpp
    ui/WindowAnimator.hpp
    ui/WindowRegistry.cpp
    ui/WindowRegistry.hpp
    ui/dialog/AppearanceSettingsPanel.cpp
    ui/dialog/AppearanceSettingsPanel.hpp
    ui/dialog/DebugSettingsPanel.cpp
    ui/dialog/DebugSettingsPanel.hpp
    ui/dialog/DialogSettingsView.cpp
    ui/dialog/DialogSettingsView.hpp
    ui/dialog/DialogWindow.cpp
    ui/dialog/DialogWindow.hpp
    ui/dialog/TranslationSettingsPanel.cpp
    ui/dialog/TranslationSettingsPanel.hpp
    ui/help/HelpWindow.cpp
    ui/help/HelpWindow.hpp
    ui/quest/QuestSettingsView.cpp
    ui/quest/QuestSettingsView.hpp
    ui/quest/QuestWindow.cpp
    ui/quest/QuestWindow.hpp
)

target_sources(dqxu_core
  PRIVATE
    translate/ITranslator.hpp
    translate/GoogleTranslator.cpp
    translate/GoogleTranslator.hpp
    translate/NiutransTranslator.cpp
    translate/NiutransTranslator.hpp
    translate/OpenAITranslator.cpp
    translate/OpenAITranslator.hpp
    translate/QwenMTTranslator.cpp
    translate/QwenMTTranslator.hpp
    translate/TranslationRequestBuilder.cpp
    translate/TranslationRequestBuilder.hpp
    translate/TranslatorConfigBuilder.cpp
    translate/TranslatorFactory.cpp
    translate/YoudaoTranslator.cpp
    translate/YoudaoTranslator.hpp
    translate/ZhipuGLMTranslator.cpp
    translate/ZhipuGLMTranslator.hpp
)

target_sources(dqxu_core
  PRIVATE
    processing/Diagnostics.cpp
    processing/Diagnostics.hpp
    processing/GlossaryManager.cpp
    processing/GlossaryManager.hpp
    processing/JapaneseTextDetector.cpp
    processing/JapaneseTextDetector.hpp
    processing/LabelProcessor.cpp
    processing/LabelProcessor.hpp
    processing/LabelRegistry.cpp
    processing/LabelRegistry.hpp
    processing/TextNormalizer.cpp
    processing/TextNormalizer.hpp
    processing/TextPipeline.cpp
    processing/TextPipeline.hpp
    processing/UnknownLabelRepository.cpp
    processing/UnknownLabelRepository.hpp
)

target_sources(dqxu_core
  PRIVATE
    platform/ProcessDetector.cpp
    platform/ProcessDetector.hpp
    platform/ProcessLocaleChecker.cpp
    platform/ProcessLocaleChecker.hpp
    platform/SingleInstanceGuard.cpp
    platform/SingleInstanceGuard.hpp
    platform/WineDetector.cpp
    platform/WineDetector.hpp
)

target_sources(dqxu_core
  PRIVATE
    services/DQXClarityLauncher.cpp
    services/DQXClarityLauncher.hpp
    services/DQXClarityService.cpp
    services/DQXClarityService.hpp
)

target_sources(dqxu_core
  PRIVATE
    config/ConfigManager.cpp
    config/ConfigManager.hpp
)

target_sources(dqxu_core
  PRIVATE
    utils/CrashHandler.cpp
    utils/CrashHandler.hpp
    utils/ErrorReporter.cpp
    utils/ErrorReporter.hpp
    utils/HttpCommon.cpp
    utils/HttpCommon.hpp
    utils/NativeMessageBox.cpp
    utils/NativeMessageBox.hpp
)

target_sources(dqxu_core
  PRIVATE
    state/QuestStateManager.hpp
)

target_include_directories(dqxu_core
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/services
  PUBLIC
    ${tomlplusplus_SOURCE_DIR}/include
    ${plog_SOURCE_DIR}/include
)

target_link_libraries(dqxu_core
  PUBLIC
    SDL3::SDL3
    imgui
    cpr::cpr
    cpptrace::cpptrace
    nlohmann_json::nlohmann_json
  PRIVATE
    project_options
    project_warnings
)

if(ENABLE_PROFILING)
  target_link_libraries(dqxu_core PUBLIC Tracy::TracyClient)
endif()

if(ENABLE_DEBUG_SECTION)
  target_compile_definitions(dqxu_core PRIVATE DQXU_ENABLE_DEBUG_SECTION)
endif()

add_executable(dqxu_app main.cpp)

configure_target_properties(dqxu_app)
configure_windows_resources(dqxu_app)

target_link_libraries(dqxu_app
  PRIVATE
    dqxu_core
    dqxclarity::dqxclarity
    project_options
    project_warnings
)

if(NOT MSVC AND HAS_NO_DEPRECATED_LITERAL_OPERATOR)
  target_compile_options(dqxu_app PRIVATE -Wno-deprecated-literal-operator)
endif()

if(WIN32)
  target_link_libraries(dqxu_app PRIVATE dbghelp)
endif()

install_runtime_dependencies(dqxu_app)

source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)