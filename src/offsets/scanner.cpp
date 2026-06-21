// ============================================================================
//  SIGNATURE SCANNER — Implementation
//  
//  Byte pattern scanning through remote process memory.
//  Reads memory in chunks for performance, compares against patterns
//  with wildcard support. Used to find GWorld and other engine pointers
//  that move with each game update.
// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "scanner.h"
#include "offsets.h"
#include "../memory/memory.h"

#include <vector>
#include <sstream>
#include <algorithm>

namespace scanner {

// ============================================================================
//  Internal state
// ============================================================================

namespace detail {
    static const mem::ProcessMemory* g_memory = nullptr;
    static uintptr_t g_moduleBase = 0;
    static size_t g_moduleSize = 0;

    // ========================================================================
    //  Pattern parsing
    //  
    //  Converts "48 8B 05 ?? ?? ?? ??" into:
    //    bytes:  [0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00]
    //    mask:   [true, true, true, false, false, false, false]
    // ========================================================================

    struct ParsedPattern {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;  // true = must match, false = wildcard
    };

    static ParsedPattern ParsePattern(const std::string& pattern) {
        ParsedPattern result;
        std::istringstream stream(pattern);
        std::string token;

        while (stream >> token) {
            if (token == "??" || token == "?") {
                result.bytes.push_back(0);
                result.mask.push_back(false);  // wildcard
            } else {
                try {
                    uint8_t byte = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
                    result.bytes.push_back(byte);
                    result.mask.push_back(true);  // must match
                } catch (...) {
                    // Invalid token — treat as wildcard
                    result.bytes.push_back(0);
                    result.mask.push_back(false);
                }
            }
        }

        return result;
    }

    // ========================================================================
    //  Core pattern matching against a buffer
    // ========================================================================

    static bool MatchPattern(
        const uint8_t* data, 
        size_t dataSize, 
        size_t offset,
        const ParsedPattern& pattern
    ) {
        if (offset + pattern.bytes.size() > dataSize) return false;

        for (size_t i = 0; i < pattern.bytes.size(); ++i) {
            if (pattern.mask[i] && data[offset + i] != pattern.bytes[i]) {
                return false;
            }
        }

        return true;
    }

    // ========================================================================
    //  Resolve a RIP-relative address
    //  
    //  Many x64 instructions use RIP-relative addressing:
    //    48 8B 1D [XX XX XX XX]    ; mov rbx, [rip + offset]
    //  
    //  The actual address = instruction_address + instruction_length + offset
    //  where offset is a signed 32-bit value at the specified position.
    // ========================================================================

    static uintptr_t ResolveRIPRelative(
        uintptr_t instructionAddress, 
        int offsetPosition,    // position of the 4-byte offset within the instruction
        int instructionLength  // total length of the instruction
    ) {
        if (!g_memory) return 0;

        // Read the 32-bit signed offset from the instruction
        int32_t relativeOffset = g_memory->Read<int32_t>(instructionAddress + offsetPosition);

        // Compute absolute address: rip (after instruction) + relative offset
        return instructionAddress + instructionLength + relativeOffset;
    }

} // namespace detail

// ============================================================================
//  Public API
// ============================================================================

void Initialize(const mem::ProcessMemory& memory) {
    detail::g_memory = &memory;
    detail::g_moduleBase = memory.GetBaseAddress();
    
    // Get module size — try to read it from the process
    // The main module name for Fortnite
    detail::g_moduleSize = memory.GetModuleSize(L"FortniteClient-Win64-Shipping.exe");
    
    // Fallback: if we can't get the module size, use a generous default
    if (detail::g_moduleSize == 0) {
        detail::g_moduleSize = 0x10000000;  // 256 MB — Fortnite is big
    }
}

ScanResult FindPattern(uintptr_t start, size_t size, const std::string& pattern) {
    ScanResult result;
    
    if (!detail::g_memory || pattern.empty()) return result;
    
    auto parsed = detail::ParsePattern(pattern);
    if (parsed.bytes.empty()) return result;

    // Read memory in chunks for performance
    constexpr size_t CHUNK_SIZE = 0x10000;  // 64 KB chunks
    std::vector<uint8_t> buffer(CHUNK_SIZE + parsed.bytes.size());

    for (size_t offset = 0; offset < size; offset += CHUNK_SIZE) {
        size_t readSize = std::min(CHUNK_SIZE + parsed.bytes.size(), size - offset);
        
        if (!detail::g_memory->ReadRaw(start + offset, buffer.data(), readSize)) {
            continue;  // Skip unreadable regions
        }

        // Scan this chunk
        for (size_t i = 0; i < readSize - parsed.bytes.size(); ++i) {
            if (detail::MatchPattern(buffer.data(), readSize, i, parsed)) {
                result.address = start + offset + i;
                result.found = true;
                return result;
            }
        }
    }

    return result;
}

ScanResult FindPatternInModule(const std::string& pattern) {
    if (detail::g_moduleBase == 0) return {};
    return FindPattern(detail::g_moduleBase, detail::g_moduleSize, pattern);
}

// ============================================================================
//  GWorld resolution
//  
//  Known UE5 signatures for GWorld access:
//  The engine accesses GWorld through a global pointer. The code pattern
//  typically looks like:
//    48 8B 1D [XX XX XX XX]    ; mov rbx, [rip + GWorld_offset]
//    48 85 DB                  ; test rbx, rbx
//    74 XX                     ; jz <somewhere>
//  
//  We scan for this pattern, then resolve the RIP-relative offset to get
//  the absolute address of the GWorld pointer.
// ============================================================================

ScanResult ResolveGWorld() {
    ScanResult result;

    // Signature patterns for GWorld access (try multiple)
    const char* signatures[] = {
        // Pattern 1: mov rbx, [rip+??]; test rbx, rbx; jz
        "48 8B 1D ?? ?? ?? ?? 48 85 DB 74",
        // Pattern 2: mov rax, [rip+??]; test rax, rax; je
        "48 8B 05 ?? ?? ?? ?? 48 85 C0 0F 84",
        // Pattern 3: alternate GWorld access
        "48 8B 3D ?? ?? ?? ?? 48 85 FF 74",
    };

    for (const char* sig : signatures) {
        auto scanResult = FindPatternInModule(sig);
        
        if (scanResult.found) {
            // The RIP-relative offset is at position 3 in all these patterns
            // Instruction is 7 bytes long (48 8B XX [4-byte offset])
            uintptr_t gworldAddr = detail::ResolveRIPRelative(
                scanResult.address,
                3,   // offset position within instruction
                7    // instruction length
            );

            if (gworldAddr != 0) {
                result.address = gworldAddr;
                result.found = true;
                
                // Store in offsets
                offsets::UWorld::GWorld = gworldAddr;
                return result;
            }
        }
    }

    return result;
}

// ============================================================================
//  Full offset resolution — tries to find as many offsets as possible
// ============================================================================

bool ResolveEngineOffsets() {
    if (!detail::g_memory) return false;

    bool success = false;

    // GWorld is the critical one — everything chains from here
    auto gworld = ResolveGWorld();
    if (gworld.found) {
        success = true;
    }

    // Future: add more signature-based resolutions here
    // For now, GWorld is the only one we can reliably sig-scan
    // The rest use hardcoded offsets from the offset config file
    
    // These could be added:
    // - GNames pattern: "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 0D"
    // - GObjects pattern: "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1"
    // - ProcessEvent pattern for hooking

    return success;
}

} // namespace scanner
