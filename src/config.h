// ============================================================================
//  CONFIGURATION SYSTEM — Runtime settings management
//  Load, save, and manage ESP settings from a JSON config file.
// ============================================================================

#pragma once

#include <Windows.h>
#include <string>

namespace config {

// ============================================================================
//  Settings struct — all configurable values in one place
// ============================================================================

struct Settings {
    // ESP feature toggles
    bool espEnabled    = true;
    bool showBoxes     = true;
    bool showSnaplines = true;
    bool showHealth    = true;
    bool showShield    = true;
    bool showDistance   = true;
    bool showNames     = true;
    bool showHeadDot   = true;
    bool showSkeleton  = false;

    // Visual parameters
    float boxThickness      = 1.5f;
    float snaplineThickness = 1.0f;
    float maxDistance        = 300.0f;  // Meters

    // Colors (RGBA, 0.0-1.0)
    float enemyColor[4] = { 1.0f, 0.3f, 0.3f, 1.0f };
    float teamColor[4]  = { 0.3f, 1.0f, 0.3f, 1.0f };

    // Keybinds (virtual key codes)
    int toggleKey    = VK_INSERT;   // Toggle menu
    int espToggleKey = VK_HOME;     // Toggle ESP on/off
    int exitKey      = VK_END;      // Exit application

    // Target process/window
    std::string targetWindow  = "Fortnite  ";
    std::string targetProcess = "FortniteClient-Win64-Shipping.exe";
};

// ============================================================================
//  Global settings instance
// ============================================================================

extern Settings g_settings;

// ============================================================================
//  File I/O
// ============================================================================

/// Load settings from a JSON config file. Returns true on success.
/// On failure, g_settings retains its default values.
bool Load(const std::string& path = "config.json");

/// Save current settings to a JSON config file.
bool Save(const std::string& path = "config.json");

} // namespace config
