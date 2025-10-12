cmake_minimum_required(VERSION 3.20)

project("DQX Utility" VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerFlags)

option(ENABLE_PROFILING "Enable profiling instrumentation" ON)
option(BUILD_DQXCLARITY_CLI "Build DQXClarity C++ CLI executable" OFF)
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
  src/translate/TranslatorFactory.cpp
)

if(WIN32)
  set(APP_ICON_RC src/app.rc)
else()
  set(APP_ICON_RC)
endif()

set(DQX_CORE_SOURCES
  src/Application.cpp
  src/ui/AppContext.cpp
  src/ui/dialog/DialogWindow.cpp
  src/ui/CommonUIComponents.hpp
  src/ui/FontManager.cpp
  src/ui/GlobalSettingsPanel.cpp
  src/ui/WindowRegistry.cpp
  src/ui/UIEventHandler.cpp
  src/ui/MiniModeManager.cpp
  src/ui/AppModeManager.cpp
  src/ui/dialog/DialogAnimator.cpp
  src/ui/dialog/DialogSettingsView.cpp
  src/ui/dialog/AppearanceSettingsPanel.cpp
  src/ui/dialog/TranslationSettingsPanel.cpp
  src/ui/dialog/DebugSettingsPanel.cpp
  src/ui/quest/QuestWindow.cpp
  src/ui/quest/QuestWindow.hpp
  src/ui/quest/QuestSettingsView.cpp
  src/ui/quest/QuestSettingsView.hpp
  src/platform/ProcessDetector.cpp
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
