// ============================================================================
//  ESP RENDERER — Implementation
//  
//  This is where the visual magic happens. DX11 creates the rendering context,
//  ImGui provides the drawing API, and we paint ESP overlays that would make
//  any game dev cry into their anti-cheat.
// ============================================================================

#include "renderer.h"
#include "../entity/entity.h"
#include "../offsets/offsets.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ImGui Win32 message handler (declared in imgui_impl_win32.h)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam
);

namespace renderer {

// ============================================================================
//  Helpers — color conversion
// ============================================================================

static ImU32 Vec4ToU32(const ImVec4& color) {
    return IM_COL32(
        static_cast<int>(color.x * 255),
        static_cast<int>(color.y * 255),
        static_cast<int>(color.z * 255),
        static_cast<int>(color.w * 255)
    );
}

// Interpolate between two colors based on a [0,1] factor
static ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    int ra = (a >> IM_COL32_R_SHIFT) & 0xFF, ga = (a >> IM_COL32_G_SHIFT) & 0xFF;
    int ba2 = (a >> IM_COL32_B_SHIFT) & 0xFF, aa = (a >> IM_COL32_A_SHIFT) & 0xFF;
    int rb = (b >> IM_COL32_R_SHIFT) & 0xFF, gb = (b >> IM_COL32_G_SHIFT) & 0xFF;
    int bb = (b >> IM_COL32_B_SHIFT) & 0xFF, ab = (b >> IM_COL32_A_SHIFT) & 0xFF;

    return IM_COL32(
        static_cast<int>(ra + (rb - ra) * t),
        static_cast<int>(ga + (gb - ga) * t),
        static_cast<int>(ba2 + (bb - ba2) * t),
        static_cast<int>(aa + (ab - aa) * t)
    );
}

// Health color: green → yellow → red as health decreases
static ImU32 HealthToColor(float percent) {
    if (percent > 0.5f) {
        // Green to yellow
        float t = 1.0f - (percent - 0.5f) * 2.0f;
        return LerpColor(IM_COL32(75, 255, 75, 255), IM_COL32(255, 255, 0, 255), t);
    } else {
        // Yellow to red
        float t = 1.0f - percent * 2.0f;
        return LerpColor(IM_COL32(255, 255, 0, 255), IM_COL32(255, 50, 50, 255), t);
    }
}

// ============================================================================
//  Lifecycle
// ============================================================================

Renderer::~Renderer() {
    Shutdown();
}

// ============================================================================
//  Initialize DX11 + ImGui
// ============================================================================

bool Renderer::Initialize(HWND overlayHwnd, int width, int height) {
    // Step 1: Create swap chain description
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.RefreshRate.Numerator = 0;  // VSync adaptive
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.OutputWindow = overlayHwnd;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // Step 2: Create device and swap chain
    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = 0;
    #ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // Hardware acceleration
        nullptr,                    // No software rasterizer
        createFlags,
        nullptr, 0,                 // Default feature levels
        D3D11_SDK_VERSION,
        &scd,
        &m_swapChain,
        &m_device,
        &featureLevel,
        &m_context
    );

    if (FAILED(hr)) return false;

    // Step 3: Create render target view
    CreateRenderTarget();
    if (!m_renderTarget) return false;

    // Step 4: Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // Don't save layout to disk (stealth)

    // Step 5: Style ImGui — make it look PREMIUM
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.Alpha = 0.95f;

    // Dark theme with accent colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_Border]           = ImVec4(0.30f, 0.30f, 0.45f, 0.50f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.10f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.45f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.45f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.30f, 0.30f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.35f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_Header]           = ImVec4(0.20f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.30f, 0.30f, 0.48f, 1.00f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.35f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_Text]             = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_Tab]              = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.35f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_TabActive]        = ImVec4(0.25f, 0.25f, 0.42f, 1.00f);
    colors[ImGuiCol_Separator]        = ImVec4(0.25f, 0.25f, 0.38f, 0.50f);

    // Step 6: Initialize ImGui backends
    ImGui_ImplWin32_Init(overlayHwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    return true;
}

// ============================================================================
//  Frame management
// ============================================================================

void Renderer::BeginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Clear render target with fully transparent black
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, clearColor);
}

void Renderer::EndFrame() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Present with VSync (1 = VSync on, 0 = off for max FPS)
    m_swapChain->Present(1, 0);
}

// ============================================================================
//  ESP Rendering — the main show
// ============================================================================

void Renderer::RenderESP(
    const std::vector<entity::PlayerEntity>& players,
    int screenWidth, int screenHeight
) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    for (const auto& player : players) {
        // Skip players that aren't on screen or are too far
        if (!player.isOnScreen) continue;
        if (player.isDead) continue;
        if (player.isLocalPlayer) continue;
        if (player.distance > m_config.maxDistance) continue;

        // Determine color based on team
        // TODO: when we have local player team index, compare properly
        ImU32 color = Vec4ToU32(m_config.enemyColor);

        // Draw each enabled ESP element
        if (m_config.showBoxes)     DrawBox(draw, player, color);
        if (m_config.showSnaplines) DrawSnapline(draw, player, screenHeight, Vec4ToU32(m_config.snaplineColor));
        if (m_config.showHealth)    DrawHealthBar(draw, player);
        if (m_config.showShield)    DrawShieldBar(draw, player);
        if (m_config.showDistance)  DrawDistance(draw, player);
        if (m_config.showNames)     DrawName(draw, player);
        if (m_config.showHeadDot)   DrawHeadDot(draw, player, color);
        if (m_config.showSkeleton && player.hasBones) DrawSkeleton(draw, player, Vec4ToU32(m_config.skeletonColor));
    }
}

// ============================================================================
//  Individual ESP drawing functions
// ============================================================================

void Renderer::DrawBox(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color) {
    const auto& box = player.box;
    if (box.height <= 0 || box.width <= 0) return;

    ImVec2 topLeft(box.left, box.top);
    ImVec2 bottomRight(box.right, box.bottom);

    // Black outline for contrast
    ImU32 outline = Vec4ToU32(m_config.outlineColor);
    draw->AddRect(
        ImVec2(topLeft.x - 1, topLeft.y - 1),
        ImVec2(bottomRight.x + 1, bottomRight.y + 1),
        outline, 0, 0, m_config.boxThickness + 2.0f
    );

    // Main box
    draw->AddRect(topLeft, bottomRight, color, 0, 0, m_config.boxThickness);
}

void Renderer::DrawCornerBox(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color) {
    const auto& box = player.box;
    if (box.height <= 0 || box.width <= 0) return;

    float cornerLen = box.height * 0.2f;  // Corner lines are 20% of box height

    ImVec2 tl(box.left, box.top);
    ImVec2 tr(box.right, box.top);
    ImVec2 bl(box.left, box.bottom);
    ImVec2 br(box.right, box.bottom);

    float thick = m_config.boxThickness;

    // Top-left corner
    draw->AddLine(tl, ImVec2(tl.x + cornerLen, tl.y), color, thick);
    draw->AddLine(tl, ImVec2(tl.x, tl.y + cornerLen), color, thick);

    // Top-right corner
    draw->AddLine(tr, ImVec2(tr.x - cornerLen, tr.y), color, thick);
    draw->AddLine(tr, ImVec2(tr.x, tr.y + cornerLen), color, thick);

    // Bottom-left corner
    draw->AddLine(bl, ImVec2(bl.x + cornerLen, bl.y), color, thick);
    draw->AddLine(bl, ImVec2(bl.x, bl.y - cornerLen), color, thick);

    // Bottom-right corner
    draw->AddLine(br, ImVec2(br.x - cornerLen, br.y), color, thick);
    draw->AddLine(br, ImVec2(br.x, br.y - cornerLen), color, thick);
}

void Renderer::DrawSnapline(
    ImDrawList* draw, const entity::PlayerEntity& player, 
    int screenHeight, ImU32 color
) {
    // Line from bottom-center of screen to player position
    ImVec2 screenBottom(static_cast<float>(screenHeight) / 2.0f, 
                        static_cast<float>(screenHeight));  // Hmm, width/2 is better
    ImVec2 playerPos(player.screenPos.x, player.box.bottom);

    draw->AddLine(
        ImVec2(960.0f, static_cast<float>(screenHeight)),  // Bottom-center (assume 1920 width)
        playerPos,
        color,
        m_config.snaplineThickness
    );
}

void Renderer::DrawHealthBar(ImDrawList* draw, const entity::PlayerEntity& player) {
    const auto& box = player.box;
    if (box.height <= 0) return;

    float barWidth = 3.0f;
    float barX = box.left - barWidth - 4.0f;  // Left of the box
    float barTop = box.top;
    float barBottom = box.bottom;
    float barHeight = barBottom - barTop;

    float healthPercent = player.HealthPercent();

    // Background (dark)
    draw->AddRectFilled(
        ImVec2(barX - 1, barTop - 1),
        ImVec2(barX + barWidth + 1, barBottom + 1),
        IM_COL32(0, 0, 0, 150)
    );

    // Health bar (fills from bottom to top)
    float fillHeight = barHeight * healthPercent;
    ImU32 healthCol = HealthToColor(healthPercent);

    draw->AddRectFilled(
        ImVec2(barX, barBottom - fillHeight),
        ImVec2(barX + barWidth, barBottom),
        healthCol
    );
}

void Renderer::DrawShieldBar(ImDrawList* draw, const entity::PlayerEntity& player) {
    const auto& box = player.box;
    if (box.height <= 0) return;
    if (player.shield <= 0) return;  // Don't draw if no shield

    float barWidth = 3.0f;
    float barX = box.right + 4.0f;  // Right of the box
    float barTop = box.top;
    float barBottom = box.bottom;
    float barHeight = barBottom - barTop;

    float shieldPercent = player.ShieldPercent();

    // Background
    draw->AddRectFilled(
        ImVec2(barX - 1, barTop - 1),
        ImVec2(barX + barWidth + 1, barBottom + 1),
        IM_COL32(0, 0, 0, 150)
    );

    // Shield bar (blue, fills from bottom to top)
    float fillHeight = barHeight * shieldPercent;
    ImU32 shieldCol = Vec4ToU32(m_config.shieldColor);

    draw->AddRectFilled(
        ImVec2(barX, barBottom - fillHeight),
        ImVec2(barX + barWidth, barBottom),
        shieldCol
    );
}

void Renderer::DrawDistance(ImDrawList* draw, const entity::PlayerEntity& player) {
    char text[32];
    snprintf(text, sizeof(text), "%.0fm", player.distance);

    ImVec2 textSize = ImGui::CalcTextSize(text);
    float centerX = (player.box.left + player.box.right) / 2.0f;
    ImVec2 pos(centerX - textSize.x / 2.0f, player.box.bottom + 3.0f);

    // Shadow
    draw->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), text);
    // Text
    draw->AddText(pos, Vec4ToU32(m_config.nameColor), text);
}

void Renderer::DrawName(ImDrawList* draw, const entity::PlayerEntity& player) {
    if (player.name.empty()) return;

    ImVec2 textSize = ImGui::CalcTextSize(player.name.c_str());
    float centerX = (player.box.left + player.box.right) / 2.0f;
    ImVec2 pos(centerX - textSize.x / 2.0f, player.box.top - textSize.y - 3.0f);

    // Shadow
    draw->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), player.name.c_str());
    // Text
    draw->AddText(pos, Vec4ToU32(m_config.nameColor), player.name.c_str());
}

void Renderer::DrawHeadDot(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color) {
    // Try to use actual head bone position
    auto it = player.boneScreenPositions.find(static_cast<int>(offsets::EBoneIndex::Head));
    if (it != player.boneScreenPositions.end()) {
        draw->AddCircleFilled(
            ImVec2(it->second.x, it->second.y),
            m_config.headDotRadius,
            color
        );
    } else {
        // Fallback: estimate head position at top of box
        float centerX = (player.box.left + player.box.right) / 2.0f;
        draw->AddCircleFilled(
            ImVec2(centerX, player.box.top + 5.0f),
            m_config.headDotRadius,
            color
        );
    }
}

void Renderer::DrawSkeleton(ImDrawList* draw, const entity::PlayerEntity& player, ImU32 color) {
    if (!player.hasBones) return;

    auto getBone = [&](offsets::EBoneIndex bone) -> const math::Vector2* {
        auto it = player.boneScreenPositions.find(static_cast<int>(bone));
        if (it != player.boneScreenPositions.end()) return &it->second;
        return nullptr;
    };

    auto drawBoneLine = [&](offsets::EBoneIndex from, offsets::EBoneIndex to) {
        auto* a = getBone(from);
        auto* b = getBone(to);
        if (a && b) {
            draw->AddLine(
                ImVec2(a->x, a->y), ImVec2(b->x, b->y),
                color, m_config.skeletonThickness
            );
        }
    };

    // Spine
    drawBoneLine(offsets::EBoneIndex::Head, offsets::EBoneIndex::Neck);
    drawBoneLine(offsets::EBoneIndex::Neck, offsets::EBoneIndex::Chest);
    drawBoneLine(offsets::EBoneIndex::Chest, offsets::EBoneIndex::Pelvis);

    // Left arm
    drawBoneLine(offsets::EBoneIndex::Chest, offsets::EBoneIndex::LeftShoulder);
    drawBoneLine(offsets::EBoneIndex::LeftShoulder, offsets::EBoneIndex::LeftElbow);
    drawBoneLine(offsets::EBoneIndex::LeftElbow, offsets::EBoneIndex::LeftHand);

    // Right arm
    drawBoneLine(offsets::EBoneIndex::Chest, offsets::EBoneIndex::RightShoulder);
    drawBoneLine(offsets::EBoneIndex::RightShoulder, offsets::EBoneIndex::RightElbow);
    drawBoneLine(offsets::EBoneIndex::RightElbow, offsets::EBoneIndex::RightHand);

    // Left leg
    drawBoneLine(offsets::EBoneIndex::Pelvis, offsets::EBoneIndex::LeftHip);
    drawBoneLine(offsets::EBoneIndex::LeftHip, offsets::EBoneIndex::LeftKnee);
    drawBoneLine(offsets::EBoneIndex::LeftKnee, offsets::EBoneIndex::LeftFoot);

    // Right leg
    drawBoneLine(offsets::EBoneIndex::Pelvis, offsets::EBoneIndex::RightHip);
    drawBoneLine(offsets::EBoneIndex::RightHip, offsets::EBoneIndex::RightKnee);
    drawBoneLine(offsets::EBoneIndex::RightKnee, offsets::EBoneIndex::RightFoot);
}

// ============================================================================
//  Settings Menu — styled and organized
// ============================================================================

void Renderer::RenderMenu(ESPConfig& config) {
    ImGui::SetNextWindowSize(ImVec2(380, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);

    ImGui::Begin("ESP Settings", nullptr, 
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    // Header
    ImGui::TextColored(ImVec4(0.45f, 0.65f, 1.0f, 1.0f), "FORTNITE ESP");
    ImGui::SameLine();
    ImGui::TextDisabled("v1.0");
    ImGui::Separator();
    ImGui::Spacing();

    // === Features Section ===
    if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Box ESP", &config.showBoxes);
        ImGui::Checkbox("Snaplines", &config.showSnaplines);
        ImGui::Checkbox("Health Bars", &config.showHealth);
        ImGui::Checkbox("Shield Bars", &config.showShield);
        ImGui::Checkbox("Distance", &config.showDistance);
        ImGui::Checkbox("Player Names", &config.showNames);
        ImGui::Checkbox("Head Dot", &config.showHeadDot);
        ImGui::Checkbox("Skeleton ESP", &config.showSkeleton);
    }

    ImGui::Spacing();

    // === Visual Settings ===
    if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Box Thickness", &config.boxThickness, 0.5f, 5.0f, "%.1f");
        ImGui::SliderFloat("Snapline Thickness", &config.snaplineThickness, 0.5f, 3.0f, "%.1f");
        ImGui::SliderFloat("Skeleton Thickness", &config.skeletonThickness, 0.5f, 3.0f, "%.1f");
        ImGui::SliderFloat("Head Dot Size", &config.headDotRadius, 2.0f, 10.0f, "%.1f");
        ImGui::SliderInt("Max Distance (m)", &config.maxDistance, 50, 500);
    }

    ImGui::Spacing();

    // === Colors ===
    if (ImGui::CollapsingHeader("Colors")) {
        ImGui::ColorEdit4("Enemy", &config.enemyColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Team", &config.teamColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Health", &config.healthColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Shield", &config.shieldColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Snapline", &config.snaplineColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Names", &config.nameColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Skeleton", &config.skeletonColor.x, ImGuiColorEditFlags_NoInputs);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Footer
    ImGui::TextDisabled("INSERT - Toggle Menu");
    ImGui::TextDisabled("HOME   - Toggle ESP");
    ImGui::TextDisabled("END    - Exit");

    ImGui::End();
}

// ============================================================================
//  DX11 Resource Management
// ============================================================================

void Renderer::CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    
    if (backBuffer) {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        backBuffer->Release();
    }
}

void Renderer::CleanupRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
}

void Renderer::Resize(int width, int height) {
    if (!m_swapChain) return;

    CleanupRenderTarget();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void Renderer::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();

    if (m_swapChain)  { m_swapChain->Release();  m_swapChain = nullptr; }
    if (m_context)    { m_context->Release();     m_context = nullptr; }
    if (m_device)     { m_device->Release();      m_device = nullptr; }
}

} // namespace renderer
