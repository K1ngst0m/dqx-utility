#pragma once

#include <string>

struct DialogStateManager;
class FontManager;
class TranslateSession;

class DebugSettingsPanel {
public:
    DebugSettingsPanel(
        DialogStateManager& state,
        FontManager& fontManager,
        TranslateSession& session
    );
    
    void render(const std::string& settingsIdSuffix);

private:
    void renderFontSection();
    void renderCacheSection();
    void renderSegmentList();
    void renderSegmentEditor();
    void renderNewSegmentInput();
    
    DialogStateManager& state_;
    FontManager& fontManager_;
    TranslateSession& session_;
};
