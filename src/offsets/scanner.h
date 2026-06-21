// ============================================================================
//  SIGNATURE SCANNER — Pattern matching in process memory
//  Find critical pointers even after game updates by searching for
//  known byte patterns rather than hardcoded addresses.
// ============================================================================

#pragma once

#include <string>
#include <cstdint>

// Forward declaration — we don't need the full header here
namespace mem { class ProcessMemory; }

namespace scanner {

// ============================================================================
//  Types
// ============================================================================

struct ScanResult {
    uintptr_t address = 0;
    bool found = false;

    operator bool() const { return found; }
};

// ============================================================================
//  Public API
// ============================================================================

/// Initialize the scanner with a reference to the process memory reader
void Initialize(const mem::ProcessMemory& memory);

/// Scan for a byte pattern in a specific memory region
/// Pattern format: "48 8B 05 ?? ?? ?? ?? 48 85 C0"
///   - Hex byte values are matched exactly
///   - ?? is a wildcard (matches any byte)
/// Returns the address of the FIRST byte of the first match
ScanResult FindPattern(uintptr_t start, size_t size, const std::string& pattern);

/// Scan for a pattern within the game's main module (.exe)
ScanResult FindPatternInModule(const std::string& pattern);

/// Attempt to auto-resolve all critical UE5 engine offsets using known
/// signature patterns. Populates the offsets namespace.
/// Returns true if at least GWorld was resolved.
bool ResolveEngineOffsets();

/// Resolve just the GWorld pointer using known signatures
ScanResult ResolveGWorld();

} // namespace scanner
