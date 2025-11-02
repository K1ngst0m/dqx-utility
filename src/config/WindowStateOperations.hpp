#pragma once

#include <string>
#include <vector>
#include <memory>
#include <toml++/toml.h>

struct BaseWindowState;
class UIWindow;
class WindowRegistry;
enum class UIWindowType;

// Interface for window-type-specific operations
// Allows ConfigManager to work with windows generically without knowing specific types
class IWindowStateOperations
{
public:
    virtual ~IWindowStateOperations() = default;

    // Get the default window's state (nullptr if none exists)
    virtual BaseWindowState* getDefaultWindowState() = 0;

    // Create a default window and return its state
    virtual BaseWindowState* createDefaultWindow(const std::string& name, const BaseWindowState* initial_state) = 0;

    // Remove the default window
    virtual void removeDefaultWindow() = 0;

    // Get all windows of this type
    virtual std::vector<UIWindow*> getAllWindows() = 0;

    // Get a specific window's state pointer
    virtual BaseWindowState* getWindowState(UIWindow* window) = 0;

    // Apply state to a specific window
    virtual void applyStateToWindow(UIWindow* window, const BaseWindowState& state) = 0;

    // Serialize state to TOML
    virtual toml::table serializeState(const std::string& name, const BaseWindowState& state) = 0;

    // Get window name
    virtual std::string getWindowName(UIWindow* window) = 0;

    // Set window name
    virtual void setWindowName(UIWindow* window, const std::string& name) = 0;

    // Mark as default
    virtual void markAsDefault(UIWindow* window) = 0;
};

// Template implementation for specific window types
template <typename WindowType, typename StateType>
class WindowStateOperations : public IWindowStateOperations
{
public:
    WindowStateOperations(WindowRegistry* registry, UIWindowType window_type)
        : registry_(registry)
        , window_type_(window_type)
    {
    }

    BaseWindowState* getDefaultWindowState() override;
    BaseWindowState* createDefaultWindow(const std::string& name, const BaseWindowState* initial_state) override;
    void removeDefaultWindow() override;
    std::vector<UIWindow*> getAllWindows() override;
    BaseWindowState* getWindowState(UIWindow* window) override;
    void applyStateToWindow(UIWindow* window, const BaseWindowState& state) override;
    toml::table serializeState(const std::string& name, const BaseWindowState& state) override;
    std::string getWindowName(UIWindow* window) override;
    void setWindowName(UIWindow* window, const std::string& name) override;
    void markAsDefault(UIWindow* window) override;

private:
    WindowRegistry* registry_;
    UIWindowType window_type_;
};
