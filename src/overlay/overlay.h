// ============================================================================
//  OVERLAY WINDOW — Transparent, topmost, click-through window
//  The invisible canvas we paint ESP on top of the game.
// ============================================================================

#pragma once

#include <Windows.h>
#include <dwmapi.h>
#include <string>

namespace overlay {

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    // No copying
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /// Create the overlay window targeting the specified game window
    bool Create(const std::wstring& targetWindowTitle);

    /// Create a fullscreen overlay on the primary monitor (demo / preview mode)
    bool CreateStandalone();

    /// Sync overlay position and size to match the target window
    void SyncToTarget();

    /// Toggle click-through mode
    /// true = clicks pass through (normal ESP mode)
    /// false = overlay captures clicks (menu interaction mode)
    void SetClickThrough(bool enabled);

    /// Process Windows messages. Returns false if the window was closed.
    bool ProcessMessages();

    HWND GetHandle() const { return m_handle; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsVisible() const { return m_handle != nullptr; }

private:
    HWND m_handle = nullptr;
    HWND m_targetWindow = nullptr;
    std::wstring m_targetTitle;
    std::wstring m_className;
    int m_width = 1920;
    int m_height = 1080;
    bool m_clickThrough = true;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    bool CreateWindowInternal(int x, int y, int width, int height);
    
    /// Generate a random window class name (anti-detection)
    std::wstring GenerateRandomClassName() const;
    
    /// Generate a random window title
    std::wstring GenerateRandomTitle() const;
};

} // namespace overlay
