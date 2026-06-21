// ============================================================================
//  OVERLAY WINDOW — Implementation
//  Creates an invisible window that floats over Fortnite. We render ESP
//  on this window. The game sees nothing. The player sees everything.
// ============================================================================

#include "overlay.h"
#include <random>
#include <chrono>

// Link DWM library
#pragma comment(lib, "dwmapi.lib")

namespace overlay {

// ============================================================================
//  Window procedure — minimal, just handles destruction
// ============================================================================

LRESULT CALLBACK OverlayWindow::WindowProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;  // We handle background clearing in DX11

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ============================================================================
//  Lifecycle
// ============================================================================

OverlayWindow::~OverlayWindow() {
    if (m_handle) {
        DestroyWindow(m_handle);
        m_handle = nullptr;
    }
    if (!m_className.empty()) {
        UnregisterClassW(m_className.c_str(), GetModuleHandleW(nullptr));
    }
}

// ============================================================================
//  Create the overlay
// ============================================================================

bool OverlayWindow::CreateWindowInternal(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    m_width = width;
    m_height = height;

    if (m_className.empty()) {
        m_className = GenerateRandomClassName();
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = m_className.c_str();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassExW(&wc)) return false;

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED
                  | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

    m_handle = CreateWindowExW(
        exStyle,
        m_className.c_str(),
        GenerateRandomTitle().c_str(),
        WS_POPUP,
        x, y,
        m_width, m_height,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!m_handle) return false;

    SetLayeredWindowAttributes(m_handle, RGB(0, 0, 0), 0, LWA_COLORKEY);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_handle, &margins);

    ShowWindow(m_handle, SW_SHOWNOACTIVATE);
    UpdateWindow(m_handle);
    return true;
}

bool OverlayWindow::Create(const std::wstring& targetWindowTitle) {
    m_targetTitle = targetWindowTitle;

    m_targetWindow = FindWindowW(nullptr, targetWindowTitle.c_str());
    if (!m_targetWindow) return false;

    RECT targetRect;
    if (!GetClientRect(m_targetWindow, &targetRect)) return false;

    POINT topLeft = { targetRect.left, targetRect.top };
    ClientToScreen(m_targetWindow, &topLeft);

    const int width = targetRect.right - targetRect.left;
    const int height = targetRect.bottom - targetRect.top;

    return CreateWindowInternal(topLeft.x, topLeft.y, width, height);
}

bool OverlayWindow::CreateStandalone() {
    m_targetWindow = nullptr;
    m_targetTitle.clear();

    m_width = GetSystemMetrics(SM_CXSCREEN);
    m_height = GetSystemMetrics(SM_CYSCREEN);

    return CreateWindowInternal(0, 0, m_width, m_height);
}

// ============================================================================
//  Sync overlay to target window
// ============================================================================

void OverlayWindow::SyncToTarget() {
    if (!m_handle || !m_targetWindow) return;

    // Check if target window still exists
    if (!IsWindow(m_targetWindow)) {
        // Try to find it again
        m_targetWindow = FindWindowW(nullptr, m_targetTitle.c_str());
        if (!m_targetWindow) return;
    }

    // Check if target is minimized
    if (IsIconic(m_targetWindow)) {
        ShowWindow(m_handle, SW_HIDE);
        return;
    }

    // Get current target position and size
    RECT targetRect;
    if (!GetClientRect(m_targetWindow, &targetRect)) return;

    POINT topLeft = { targetRect.left, targetRect.top };
    ClientToScreen(m_targetWindow, &topLeft);

    int newWidth = targetRect.right - targetRect.left;
    int newHeight = targetRect.bottom - targetRect.top;

    // Only move/resize if something changed
    if (newWidth != m_width || newHeight != m_height) {
        m_width = newWidth;
        m_height = newHeight;
    }

    // Move the overlay to match
    SetWindowPos(
        m_handle, HWND_TOPMOST,
        topLeft.x, topLeft.y,
        m_width, m_height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

// ============================================================================
//  Click-through toggle
// ============================================================================

void OverlayWindow::SetClickThrough(bool enabled) {
    if (!m_handle || m_clickThrough == enabled) return;

    LONG_PTR exStyle = GetWindowLongPtrW(m_handle, GWL_EXSTYLE);

    if (enabled) {
        // Add WS_EX_TRANSPARENT — clicks pass through
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        // Remove WS_EX_TRANSPARENT — overlay captures clicks (for menu)
        exStyle &= ~WS_EX_TRANSPARENT;
    }

    SetWindowLongPtrW(m_handle, GWL_EXSTYLE, exStyle);
    m_clickThrough = enabled;
}

// ============================================================================
//  Message processing
// ============================================================================

bool OverlayWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ============================================================================
//  Random name generation (anti-detection)
// ============================================================================

std::wstring OverlayWindow::GenerateRandomClassName() const {
    static const wchar_t charset[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    auto seed = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, wcslen(charset) - 1);
    std::uniform_int_distribution<int> lenDist(8, 14);

    int length = lenDist(rng);
    std::wstring result;
    result.reserve(length);

    for (int i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }

    return result;
}

std::wstring OverlayWindow::GenerateRandomTitle() const {
    // Generate something that looks like a legit Windows process/app title
    static const wchar_t* fakeTitles[] = {
        L"Microsoft Text Input Application",
        L"Windows Shell Experience Host",
        L"Desktop Window Manager",
        L"COM Surrogate",
        L"Runtime Broker",
        L"Windows System Guard",
        L"Security Health Service",
        L"Background Task Host",
    };

    auto seed = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, 7);

    return fakeTitles[dist(rng)];
}

} // namespace overlay
