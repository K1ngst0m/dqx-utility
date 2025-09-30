#pragma once

#include "UIState.hpp"
#include "IPCConfig.hpp"
#include "TranslationConfig.hpp"
#include "ContentState.hpp"

struct DialogStateManager
{
    UIState ui;
    IPCConfig ipc;
    TranslationConfig translation;
    ContentState content;

    UIState& ui_state() { return ui; }
    const UIState& ui_state() const { return ui; }

    IPCConfig& ipc_config() { return ipc; }
    const IPCConfig& ipc_config() const { return ipc; }

    TranslationConfig& translation_config() { return translation; }
    const TranslationConfig& translation_config() const { return translation; }

    ContentState& content_state() { return content; }
    const ContentState& content_state() const { return content; }
};