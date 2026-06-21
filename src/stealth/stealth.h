// ============================================================================
//  STEALTH SYSTEM — Anti-detection measures
//  Hide from screen capture, detect debuggers, obfuscate strings.
//  Every layer counts when EAC is watching.
// ============================================================================

#pragma once

#include <Windows.h>
#include <string>
#include <array>

namespace stealth {

// ============================================================================
//  Public API
// ============================================================================

/// Initialize all stealth measures. Call at startup before anything else.
bool Initialize();

/// Hide the overlay window from screen capture (OBS, Discord, etc.)
/// Uses SetWindowDisplayAffinity which makes the window invisible to
/// any screen capture API including BitBlt, PrintWindow, etc.
bool HideFromCapture(HWND overlayWindow);

/// Check if a debugger is attached. Returns true if detected.
/// Uses multiple detection methods for resilience.
bool CheckForDebugger();

/// Set the overlay window title to a random string (periodic obfuscation)
void RandomizeWindowTitle(HWND window);

/// Clean up traces on exit — zero sensitive memory, close handles
void Cleanup();

// ============================================================================
//  Compile-time string obfuscation
//  
//  Encrypts string literals at compile time with XOR so they don't appear
//  in the binary's string table. Decrypted at runtime when needed.
// ============================================================================

namespace detail {

    constexpr char XOR_KEY = 0x5A;

    template<size_t N>
    struct ObfuscatedString {
        std::array<char, N> data;

        // Encrypt at compile time
        constexpr ObfuscatedString(const char(&str)[N]) : data{} {
            for (size_t i = 0; i < N; ++i) {
                data[i] = str[i] ^ XOR_KEY;
            }
        }

        // Decrypt at runtime
        std::string Decrypt() const {
            std::string result(data.data(), N - 1);  // -1 for null terminator
            for (auto& c : result) {
                c ^= XOR_KEY;
            }
            return result;
        }
    };

} // namespace detail

/// Macro for compile-time string encryption
/// Usage: OBFSTR("FortniteClient") → returns a std::string at runtime
#define OBFSTR(str) ([]() -> std::string { \
    constexpr auto obf = stealth::detail::ObfuscatedString(str); \
    return obf.Decrypt(); \
}())

} // namespace stealth
