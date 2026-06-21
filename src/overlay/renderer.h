// ============================================================================
//  ESP RENDERER — DirectX 11 + Dear ImGui rendering
//  Draws boxes, snaplines, health bars, skeletons — the whole show.
// ============================================================================

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <vector>

#include "imgui.h"

// Forward declaration
namespace entity { struct PlayerEntity; }

namespace renderer {

// ============================================================================
//  ESP visual configuration
// ============================================================================

struct ESPConfig {
    // Feature toggles
    bool showBoxes      = true;
    bool showSnaplines  = true;
    bool showHealth     = true;
    bool showShield     = true;
    bool showDistance    = true;
    bool showNames      = true;
    bool showHeadDot    = true;
    bool showSkeleton   = false;

    // Colors (RGBA float, 0.0-1.0)
    ImVec4 enemyColor     = ImVec4(1.0f, 0.30f, 0.30f, 1.0f);   // Warm red
    ImVec4 teamColor      = ImVec4(0.30f, 1.0f, 0.50f, 1.0f);   // Green
    ImVec4 healthColor    = ImVec4(0.30f, 1.0f, 0.30f, 1.0f);   // Green
    ImVec4 shieldColor    = ImVec4(0.30f, 0.50f, 1.0f, 1.0f);   // Blue
    ImVec4 snaplineColor  = ImVec4(1.0f, 1.0f, 1.0f, 0.40f);   // White (transparent)
    ImVec4 nameColor      = ImVec4(1.0f, 1.0f, 1.0f, 0.90f);   // White
    ImVec4 skeletonColor  = ImVec4(1.0f, 1.0f, 0.0f, 0.85f);   // Yellow
    ImVec4 outlineColor   = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);   // Black outline

    // Rendering params
    float boxThickness      = 1.5f;
    float snaplineThickness = 1.0f;
    float skeletonThickness = 1.5f;
    float headDotRadius     = 4.0f;
    int maxDistance          = 300;   // Meters — don't render beyond this
};

// ============================================================================
//  Renderer class
// ============================================================================

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    // No copying
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Initialize DirectX 11 and Dear ImGui
    bool Initialize(HWND overlayHwnd, int width, int height);

    /// Begin a new render frame
    void BeginFrame();

    /// Render ESP overlays for all entities
    void RenderESP(
        const std::vector<entity::PlayerEntity>& players,
        int screenWidth, int screenHeight
    );

    /// Render the ImGui settings menu
    void RenderMenu(ESPConfig& config);

    /// End frame and present to screen
    void EndFrame();

    /// Handle window resize
    void Resize(int width, int height);

    /// Clean up all resources
    void Shutdown();

    /// Get mutable reference to config
    ESPConfig& GetConfig() { return m_config; }

private:
    // DirectX 11 objects
    ID3D11Device*           m_device       = nullptr;
    ID3D11DeviceContext*    m_context      = nullptr;
    IDXGISwapChain*         m_swapChain    = nullptr;
    ID3D11RenderTargetView* m_renderTarget = nullptr;

    ESPConfig m_config;
    bool m_menuVisible = false;

    // ====== Drawing helpers (using ImGui draw list) ======

    void DrawBox(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color);
    void DrawCornerBox(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color);
    void DrawSnapline(ImDrawList* draw, const entity::PlayerEntity& player, int screenHeight, ImU32 color);
    void DrawHealthBar(ImDrawList* draw, const entity::PlayerEntity& player);
    void DrawShieldBar(ImDrawList* draw, const entity::PlayerEntity& player);
    void DrawDistance(ImDrawList* draw, const entity::PlayerEntity& player);
    void DrawName(ImDrawList* draw, const entity::PlayerEntity& player);
    void DrawHeadDot(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color);
    void DrawSkeleton(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color);

    // ====== DX11 helpers ======
    
    void CreateRenderTarget();
    void CleanupRenderTarget();
};

} // namespace renderer
