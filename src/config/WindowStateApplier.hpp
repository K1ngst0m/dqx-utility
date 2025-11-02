#pragma once

struct BaseWindowState;
class DialogWindow;
class QuestWindow;
class QuestHelperWindow;
struct DialogStateManager;
struct QuestStateManager;
struct QuestHelperStateManager;

// Centralized window state application logic
// Handles state assignment, sanitization, font binding, and translator initialization
class WindowStateApplier
{
public:
    // Apply state to a window (type-specific)
    static void apply(DialogWindow& window, const DialogStateManager& state);
    static void apply(QuestWindow& window, const QuestStateManager& state);
    static void apply(QuestHelperWindow& window, const QuestHelperStateManager& state);
    
    // Sanitize window state for storage/restoration
    static void sanitizeWindowState(BaseWindowState& state);
};

