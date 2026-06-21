// ============================================================================
//  OFFSET DEFINITIONS — UE5 / Fortnite memory layout
//  
//  These are PLACEHOLDER values. They change with every Fortnite patch.
//  The signature scanner attempts to resolve critical ones at runtime.
//  For the rest, update offsets.json manually per game version.
//
//  Sources for current offsets:
//    - UnknownCheats Fortnite section
//    - Community SDK dumps (UEDumper output)
//    - Manual reverse engineering with IDA/Ghidra
// ============================================================================

#pragma once

#include <cstdint>
#include <string>

namespace offsets {

// ============================================================================
//  Unreal Engine 5 Core
// ============================================================================

struct UWorld {
    static inline uintptr_t GWorld = 0;             // Global UWorld* (sig scanned)
    static inline uintptr_t PersistentLevel = 0x38;
    static inline uintptr_t OwningGameInstance = 0x1B8;
    static inline uintptr_t GameState = 0x160;
};

struct UGameInstance {
    static inline uintptr_t LocalPlayers = 0x40;    // TArray<ULocalPlayer*>
};

struct ULocalPlayer {
    static inline uintptr_t PlayerController = 0x38;
};

struct APlayerController {
    static inline uintptr_t PlayerCameraManager = 0x348;
    static inline uintptr_t AcknowledgedPawn = 0x330;
};

// ============================================================================
//  Level & Actors
// ============================================================================

struct ULevel {
    static inline uintptr_t ActorArray = 0x98;      // TArray<AActor*>
    static inline uintptr_t ActorCount = 0xA0;      // int32 (num elements)
};

struct AActor {
    static inline uintptr_t RootComponent = 0x198;
    static inline uintptr_t bHidden = 0x64;
    static inline uintptr_t CustomTimeDilation = 0x68;
};

// ============================================================================
//  Scene Components
// ============================================================================

struct USceneComponent {
    static inline uintptr_t RelativeLocation = 0x128;  // FVector (x, y, z)
    static inline uintptr_t ComponentVelocity = 0x168;
};

// ============================================================================
//  Player State
// ============================================================================

struct APlayerState {
    static inline uintptr_t PawnPrivate = 0x308;
    static inline uintptr_t PlayerNamePrivate = 0x318;  // FString
    static inline uintptr_t TeamIndex = 0x1231;         // uint8
};

// ============================================================================
//  Fortnite Player Pawn
// ============================================================================

struct AFortPlayerPawn {
    static inline uintptr_t PlayerState = 0x2B0;
    static inline uintptr_t Mesh = 0x318;               // USkeletalMeshComponent*
    static inline uintptr_t CurrentHealth = 0x8B8;       // float
    static inline uintptr_t MaxHealth = 0x8BC;           // float
    static inline uintptr_t CurrentShield = 0x8C0;       // float
    static inline uintptr_t MaxShield = 0x8C4;           // float
    static inline uintptr_t bIsDying = 0x738;            // bool
};

// ============================================================================
//  Camera
// ============================================================================

struct APlayerCameraManager {
    static inline uintptr_t CameraCache = 0x1AB0;
    static inline uintptr_t CachePOV = 0x10;            // offset within CameraCache
    static inline uintptr_t POV_Location = 0x0;         // FVector within MinimalViewInfo
    static inline uintptr_t POV_Rotation = 0xC;         // FRotator within MinimalViewInfo
    static inline uintptr_t POV_FOV = 0x18;             // float within MinimalViewInfo
};

// ============================================================================
//  Skeletal Mesh (for bone/skeleton ESP)
// ============================================================================

struct USkeletalMeshComponent {
    static inline uintptr_t BoneArray = 0x5B0;          // TArray<FTransform>
    static inline uintptr_t BoneCount = 0x5B8;          // int32
    static inline uintptr_t ComponentToWorld = 0x1E0;    // FTransform
};

// ============================================================================
//  Bone indices — skeleton topology for ESP drawing
//  These are Fortnite-specific and may vary by skin/model
// ============================================================================

enum class EBoneIndex : int {
    Root        = 0,
    Pelvis      = 2,
    Spine1      = 4,
    Spine2      = 5,
    Spine3      = 6,
    Chest       = 7,
    Neck        = 67,
    Head        = 110,
    
    LeftShoulder  = 9,
    LeftElbow     = 10,
    LeftHand      = 11,
    
    RightShoulder = 38,
    RightElbow    = 39,
    RightHand     = 40,
    
    LeftHip       = 71,
    LeftKnee      = 72,
    LeftFoot      = 73,
    
    RightHip      = 78,
    RightKnee     = 79,
    RightFoot     = 80
};

// ============================================================================
//  Serialization — load/save offsets from JSON config
// ============================================================================

/// Load offset values from a JSON file. Returns true on success.
/// On failure, current values remain unchanged.
bool LoadFromFile(const std::string& path);

/// Save current offset values to a JSON file.
bool SaveToFile(const std::string& path);

} // namespace offsets
