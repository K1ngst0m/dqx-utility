cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerFlags)

option(ENABLE_PROFILING "Enable profiling instrumentation" OFF)
option(BUILD_DQXCLARITY_CLI "Build DQXClarity C++ CLI executable" OFF)
option(ENABLE_DEBUG_SECTION "Enable debugging sections in the UI" OFF)
add_subdirectory(src/dqxclarity)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(BUILD_TESTS "Build tests" ON)
else()
  option(BUILD_TESTS "Build tests" OFF)
endif()

include(Dependencies)
include(InstallDependencies)
include(Testing)

set(MAIN_TRANSLATOR_SOURCES
  src/translate/ITranslator.hpp
  src/translate/OpenAITranslator.cpp
  src/translate/OpenAITranslator.hpp
  src/translate/GoogleTranslator.cpp
  src/translate/GoogleTranslator.hpp
  src/translate/ZhipuGLMTranslator.cpp
  src/translate/ZhipuGLMTranslator.hpp
  src/translate/QwenMTTranslator.cpp
  src/translate/QwenMTTranslator.hpp
  src/translate/NiutransTranslator.cpp
  src/translate/NiutransTranslator.hpp
  src/translate/YoudaoTranslator.cpp
  src/translate/YoudaoTranslator.hpp
  src/translate/HttpCommon.cpp
  src/translate/HttpCommon.hpp
  src/translate/TranslatorConfigBuilder.cpp
  src/translate/TranslatorFactory.cpp
)

if(WIN32)
  set(DQX_APP_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  file(TO_CMAKE_PATH "${DQX_APP_ASSET_DIR}" DQX_APP_ASSET_DIR)

  set(DQX_APP_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
  if(DQX_APP_VERSION_MAJOR STREQUAL "")
    set(DQX_APP_VERSION_MAJOR 0)
  endif()
  set(DQX_APP_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
  if(DQX_APP_VERSION_MINOR STREQUAL "")
    set(DQX_APP_VERSION_MINOR 0)
  endif()
  set(DQX_APP_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
  if(DQX_APP_VERSION_PATCH STREQUAL "")
    set(DQX_APP_VERSION_PATCH 0)
  endif()
  set(DQX_APP_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")
  if(DQX_APP_VERSION_TWEAK STREQUAL "")
    set(DQX_APP_VERSION_TWEAK 0)
  endif()

  set(APP_VERSION_COMMA "${DQX_APP_VERSION_MAJOR},${DQX_APP_VERSION_MINOR},${DQX_APP_VERSION_PATCH},${DQX_APP_VERSION_TWEAK}")
  set(APP_VERSION_STRING "${DQX_APP_VERSION_MAJOR}.${DQX_APP_VERSION_MINOR}.${DQX_APP_VERSION_PATCH}.${DQX_APP_VERSION_TWEAK}")

  set(ASSET_DIR "${DQX_APP_ASSET_DIR}")

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/app.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    @ONLY
  )

  set(APP_ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
  unset(ASSET_DIR)
else()
  set(APP_ICON_RC)
endif()

set(DQX_CORE_SOURCES
  src/app/Application.cpp
  src/ui/AppContext.cpp
  src/ui/dialog/DialogWindow.cpp
  src/ui/UIHelper.hpp
  src/ui/FontManager.cpp
  src/ui/GlobalSettingsPanel.cpp
  src/ui/WindowRegistry.cpp
  src/ui/UIEventHandler.cpp
  src/ui/MiniModeManager.cpp
  src/ui/AppModeManager.cpp
  src/ui/WindowAnimator.cpp
  src/ui/dialog/DialogSettingsView.cpp
  src/ui/dialog/AppearanceSettingsPanel.cpp
  src/ui/dialog/TranslationSettingsPanel.cpp
  src/ui/dialog/DebugSettingsPanel.cpp
  src/ui/quest/QuestWindow.cpp
  src/ui/quest/QuestWindow.hpp
  src/ui/quest/QuestSettingsView.cpp
  src/ui/quest/QuestSettingsView.hpp
  src/ui/help/HelpWindow.cpp
  src/ui/help/HelpWindow.hpp
  src/platform/ProcessDetector.cpp
  src/platform/SingleInstanceGuard.cpp
  src/platform/ProcessLocaleChecker.cpp
  src/platform/WineDetector.cpp
  src/services/DQXClarityLauncher.cpp
  src/services/DQXClarityService.cpp
  src/ui/UITheme.cpp
  src/ui/Localization.cpp
  src/ui/DockState.cpp
  ${MAIN_TRANSLATOR_SOURCES}
  src/translate/LabelProcessor.cpp
  src/translate/LabelProcessor.hpp
  src/translate/TranslationRequestBuilder.cpp
  src/translate/UnknownLabelRepository.cpp
  src/processing/TextPipeline.cpp
  src/processing/Diagnostics.cpp
  src/processing/TextNormalizer.cpp
  src/processing/JapaneseTextDetector.cpp
  src/processing/JapaneseTextDetector.hpp
  src/state/QuestStateManager.hpp
  src/config/ConfigManager.cpp
  src/config/ConfigManager.hpp
  src/utils/ErrorReporter.cpp
  src/utils/ErrorReporter.hpp
  src/utils/NativeMessageBox.cpp
  src/utils/NativeMessageBox.hpp
  src/utils/CrashHandler.cpp
  src/utils/CrashHandler.hpp
  src/ui/ErrorDialog.cpp
  src/ui/ErrorDialog.hpp
)

add_library(dqx_core STATIC ${DQX_CORE_SOURCES})
target_include_directories(dqx_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services
)
set_project_warnings(dqx_core)
configure_target_properties(dqx_core)

target_link_libraries(dqx_core PUBLIC SDL3::SDL3 imgui cpr::cpr cpptrace::cpptrace)
if(ENABLE_PROFILING)
  target_link_libraries(dqx_core PUBLIC Tracy::TracyClient)
  target_compile_definitions(dqx_core PUBLIC DQX_ENABLE_PROFILING)
endif()
target_include_directories(dqx_core PUBLIC ${tomlplusplus_SOURCE_DIR}/include ${plog_SOURCE_DIR}/include)
target_compile_definitions(dqx_core PRIVATE PLOG_CHAR_IS_UTF8=1)

if (ENABLE_DEBUG_SECTION)
  target_compile_definitions(dqx_core PRIVATE DQXU_ENABLE_DEBUG_SECTION)
endif()

add_executable(dqx_utility
  src/main.cpp
  ${APP_ICON_RC}
)

set_project_warnings(dqx_utility)
configure_target_properties(dqx_utility)

target_link_libraries(dqx_utility PRIVATE 
  dqx_core
  SDL3::SDL3 
  imgui 
  dqxclarity::dqxclarity
)

if(cpr_ADDED)
  target_link_libraries(dqx_utility PRIVATE cpr::cpr)
endif()

if(tomlplusplus_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${tomlplusplus_SOURCE_DIR}/include)
  if(NOT MSVC AND HAS_NO_DEPRECATED_LITERAL_OPERATOR)
    target_compile_options(dqx_utility PRIVATE -Wno-deprecated-literal-operator)
  endif()
endif()

if(plog_SOURCE_DIR)
  target_include_directories(dqx_utility PRIVATE ${plog_SOURCE_DIR}/include)
  if(WIN32)
    target_compile_definitions(dqx_utility PRIVATE PLOG_CHAR_IS_UTF8=1)
  endif()
endif()

if(WIN32)
  target_link_libraries(dqx_utility PRIVATE dbghelp)
endif()

install_runtime_dependencies(dqx_utility)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES src/main.cpp)

include(Packaging)
