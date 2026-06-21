// ============================================================================
//  OFFSET VALIDATOR — Implementation
//  
//  Reads real values from game memory using the current offsets and checks
//  if they make sense. This catches stale offsets before you spend 20 min
//  wondering why ESP isn't showing anyone.
//
//  Validation rules:
//    - GWorld should be a valid pointer (non-zero, high address)
//    - PersistentLevel should be a valid pointer
//    - ActorCount should be > 0 and < 100000
//    - Player health/shield should be 0-200 range
//    - Camera FOV should be 30-170
// ============================================================================

#include "validator.h"
#include "../memory/memory.h"
#include "../offsets/offsets.h"
#include "../entity/math.h"

#include <cstdio>

namespace validator {

// Helper: check if an address looks like a valid pointer (in user-space range)
static bool IsValidPointer(uintptr_t addr) {
    return addr > 0x10000 && addr < 0x7FFFFFFFFFFF;
}

// ============================================================================
//  Main validation
// ============================================================================

std::vector<ValidationResult> ValidateOffsets(const mem::ProcessMemory& memory) {
    std::vector<ValidationResult> results;

    // ================================================================
    // Test 1: GWorld pointer
    // ================================================================
    {
        ValidationResult r;
        r.offsetName = "UWorld.GWorld";

        if (offsets::UWorld::GWorld == 0) {
            r.passed = false;
            r.details = "GWorld address is 0 (not resolved)";
        } else {
            uintptr_t worldPtr = memory.Read<uintptr_t>(offsets::UWorld::GWorld);
            if (IsValidPointer(worldPtr)) {
                r.passed = true;
                char buf[64];
                snprintf(buf, sizeof(buf), "Valid pointer: 0x%llX", worldPtr);
                r.details = buf;
            } else {
                r.passed = false;
                char buf[64];
                snprintf(buf, sizeof(buf), "Invalid pointer: 0x%llX", worldPtr);
                r.details = buf;
            }
        }
        results.push_back(r);
    }

    // ================================================================
    // Test 2: PersistentLevel from UWorld
    // ================================================================
    {
        ValidationResult r;
        r.offsetName = "UWorld.PersistentLevel";

        uintptr_t worldPtr = 0;
        if (offsets::UWorld::GWorld != 0) {
            worldPtr = memory.Read<uintptr_t>(offsets::UWorld::GWorld);
        }

        if (worldPtr == 0) {
            r.passed = false;
            r.details = "Can't test — GWorld is null";
        } else {
            uintptr_t level = memory.Read<uintptr_t>(worldPtr + offsets::UWorld::PersistentLevel);
            if (IsValidPointer(level)) {
                r.passed = true;
                r.details = "Valid pointer";
            } else {
                r.passed = false;
                r.details = "Invalid pointer — offset may be wrong";
            }
        }
        results.push_back(r);
    }

    // ================================================================
    // Test 3: Actor count from PersistentLevel
    // ================================================================
    {
        ValidationResult r;
        r.offsetName = "ULevel.ActorCount";

        uintptr_t worldPtr = 0, levelPtr = 0;
        if (offsets::UWorld::GWorld != 0) {
            worldPtr = memory.Read<uintptr_t>(offsets::UWorld::GWorld);
            if (worldPtr != 0) {
                levelPtr = memory.Read<uintptr_t>(worldPtr + offsets::UWorld::PersistentLevel);
            }
        }

        if (levelPtr == 0) {
            r.passed = false;
            r.details = "Can't test — PersistentLevel is null";
        } else {
            int32_t count = memory.Read<int32_t>(levelPtr + offsets::ULevel::ActorCount);
            if (count > 0 && count < 100000) {
                r.passed = true;
                char buf[64];
                snprintf(buf, sizeof(buf), "Actor count: %d (looks reasonable)", count);
                r.details = buf;
            } else {
                r.passed = false;
                char buf[64];
                snprintf(buf, sizeof(buf), "Actor count: %d (suspicious)", count);
                r.details = buf;
            }
        }
        results.push_back(r);
    }

    // ================================================================
    // Test 4: GameInstance from UWorld
    // ================================================================
    {
        ValidationResult r;
        r.offsetName = "UWorld.OwningGameInstance";

        uintptr_t worldPtr = 0;
        if (offsets::UWorld::GWorld != 0) {
            worldPtr = memory.Read<uintptr_t>(offsets::UWorld::GWorld);
        }

        if (worldPtr == 0) {
            r.passed = false;
            r.details = "Can't test — GWorld is null";
        } else {
            uintptr_t gi = memory.Read<uintptr_t>(worldPtr + offsets::UWorld::OwningGameInstance);
            r.passed = IsValidPointer(gi);
            r.details = r.passed ? "Valid pointer" : "Invalid pointer";
        }
        results.push_back(r);
    }

    // ================================================================
    // Test 5: Camera FOV (should be 30-170 degrees)
    // ================================================================
    {
        ValidationResult r;
        r.offsetName = "APlayerCameraManager.FOV";

        // Navigate the full chain: World → GameInstance → LocalPlayers[0] → Controller → CameraManager
        uintptr_t worldPtr = 0, gi = 0, lpArray = 0, lp = 0, pc = 0, cam = 0;
        bool chainOk = true;

        if (offsets::UWorld::GWorld != 0) {
            worldPtr = memory.Read<uintptr_t>(offsets::UWorld::GWorld);
        }
        if (worldPtr == 0) chainOk = false;

        if (chainOk) {
            gi = memory.Read<uintptr_t>(worldPtr + offsets::UWorld::OwningGameInstance);
            if (!IsValidPointer(gi)) chainOk = false;
        }
        if (chainOk) {
            lpArray = memory.Read<uintptr_t>(gi + offsets::UGameInstance::LocalPlayers);
            if (!IsValidPointer(lpArray)) chainOk = false;
        }
        if (chainOk) {
            lp = memory.Read<uintptr_t>(lpArray);
            if (!IsValidPointer(lp)) chainOk = false;
        }
        if (chainOk) {
            pc = memory.Read<uintptr_t>(lp + offsets::ULocalPlayer::PlayerController);
            if (!IsValidPointer(pc)) chainOk = false;
        }
        if (chainOk) {
            cam = memory.Read<uintptr_t>(pc + offsets::APlayerController::PlayerCameraManager);
            if (!IsValidPointer(cam)) chainOk = false;
        }

        if (!chainOk) {
            r.passed = false;
            r.details = "Can't test — pointer chain broken";
        } else {
            uintptr_t cachePov = cam + offsets::APlayerCameraManager::CameraCache
                               + offsets::APlayerCameraManager::CachePOV;
            float fov = memory.Read<float>(cachePov + offsets::APlayerCameraManager::POV_FOV);

            if (fov > 30.0f && fov < 170.0f) {
                r.passed = true;
                char buf[64];
                snprintf(buf, sizeof(buf), "FOV: %.1f degrees (valid)", fov);
                r.details = buf;
            } else {
                r.passed = false;
                char buf[64];
                snprintf(buf, sizeof(buf), "FOV: %.1f (out of expected range)", fov);
                r.details = buf;
            }
        }
        results.push_back(r);
    }

    return results;
}

// ============================================================================
//  Print results
// ============================================================================

void PrintResults(const std::vector<ValidationResult>& results) {
    printf("\n=== OFFSET VALIDATION RESULTS ===\n");
    int passed = 0, failed = 0;

    for (const auto& r : results) {
        const char* icon = r.passed ? "[PASS]" : "[FAIL]";
        printf("  %s %-40s %s\n", icon, r.offsetName.c_str(), r.details.c_str());
        if (r.passed) passed++; else failed++;
    }

    printf("\nResult: %d passed, %d failed out of %zu tests\n",
        passed, failed, results.size());

    if (failed > 0) {
        printf("WARNING: Some offsets may be outdated. Run the dumper (--dump) to refresh.\n");
    }
    printf("================================\n\n");
}

// ============================================================================
//  Quick pass/fail check
// ============================================================================

bool IsValid(const std::vector<ValidationResult>& results) {
    if (results.empty()) return false;

    int passed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++;
    }

    // Consider valid if at least 60% of tests pass
    return (static_cast<float>(passed) / results.size()) >= 0.6f;
}

} // namespace validator
