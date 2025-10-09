#include "MiniModeManager.hpp"
#include "AppContext.hpp"
#include "WindowRegistry.hpp"
#include "dialog/DialogWindow.hpp"
#include <plog/Log.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SDL3/SDL.h>
#endif

namespace ui {

MiniModeManager::MiniModeManager(AppContext& app_context, WindowRegistry& registry)
    : app_context_(app_context)
    , registry_(registry)
{
}

void MiniModeManager::HandleAltDrag()
{
    ImGuiIO& io = ImGui::GetIO();
    
    // Only enable drag when Alt key is held
    if (io.KeyAlt)
    {
        // Show hand cursor when Alt is held
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        
        // Trigger native OS window drag on Alt+Click (once per drag)
        if (!drag_triggered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            PLOG_INFO << "[Mini-Drag] Starting native window drag (Alt+Drag)";
#ifdef _WIN32
            SDL_Window* sdl_win = app_context_.window();
            if (sdl_win)
            {
                HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_win), 
                                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
                if (hwnd)
                {
                    ReleaseCapture();
                    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
            }
#endif
            drag_triggered_ = true;
        }
    }
    
    // Reset drag trigger when mouse is released
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        drag_triggered_ = false;
    }
}

ImGuiID MiniModeManager::SetupDockspace()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    
    // Get background alpha from first dialog (if any) to match transparency
    float background_alpha = 1.0f;
    auto dialogs = registry_.windowsByType(UIWindowType::Dialog);
    if (!dialogs.empty())
    {
        if (auto* dialog = dynamic_cast<DialogWindow*>(dialogs[0]))
        {
            background_alpha = dialog->state().ui_state().background_alpha;
        }
    }
    
    // Set window background to match dialog transparency
    ImGui::SetNextWindowBgAlpha(background_alpha);
    
    ImGuiWindowFlags container_flags = 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    
    ImGuiID dockspace_id = 0;
    if (ImGui::Begin("MiniContainer###MiniContainer", nullptr, container_flags))
    {
        ImGuiDockNodeFlags dock_flags = 
            ImGuiDockNodeFlags_NoSplit | 
            ImGuiDockNodeFlags_NoResize | 
            ImGuiDockNodeFlags_NoUndocking;
        
        dockspace_id = ImGui::GetID("DockSpace_MiniContainer");
        ImGui::DockSpace(dockspace_id, ImVec2(0, 0), dock_flags);
    }
    ImGui::End();
    
    return dockspace_id;
}

void MiniModeManager::RestoreDialogsFromMiniMode()
{
    float y_offset = 0.0f;
    for (auto& window : registry_.windows())
    {
        if (!window || window->type() != UIWindowType::Dialog)
            continue;
            
        if (auto* dw = dynamic_cast<DialogWindow*>(window.get()))
        {
            auto& ui = dw->state().ui_state();
            ui.width = 800.0f;
            ui.height = 600.0f;
            ui.window_pos = ImVec2(0.0f, y_offset);
            ui.pending_resize = true;
            ui.pending_reposition = true;
            y_offset += 40.0f;
        }
    }
}

} // namespace ui
