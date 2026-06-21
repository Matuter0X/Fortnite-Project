// ============================================================================
//  ENTITY SYSTEM — Implementation
//  
//  The core ESP logic: navigate UE5's object hierarchy in Fortnite's memory,
//  find all player pawns, read their position/health/team/bones, project
//  to screen space, and package everything for the renderer.
//
//  Memory traversal path:
//    GWorld → PersistentLevel → ActorArray → [each actor] → ...
//    GWorld → GameInstance → LocalPlayers[0] → PlayerController → CameraManager → VP Matrix
// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "entity.h"
#include "../memory/memory.h"
#include "../offsets/offsets.h"

#include <algorithm>

namespace entity {

// ============================================================================
//  Constants
// ============================================================================

// Estimated player height in Unreal units (standing)
constexpr float PLAYER_HEIGHT_UU = 180.0f;

// Maximum number of actors to iterate (safety limit)
constexpr int MAX_ACTORS = 10000;

// ============================================================================
//  Initialization
// ============================================================================

void EntityList::Initialize(const mem::ProcessMemory* memory) {
    m_memory = memory;
}

// ============================================================================
//  Main update — called once per frame
// ============================================================================

void EntityList::Update(int screenWidth, int screenHeight) {
    m_players.clear();

    if (!m_memory || !m_memory->IsAttached()) return;

    // Step 1: Read GWorld pointer
    if (offsets::UWorld::GWorld == 0) return;
    m_worldAddress = m_memory->Read<uintptr_t>(offsets::UWorld::GWorld);
    if (m_worldAddress == 0) return;

    // Step 2: Get the view-projection matrix from the camera
    math::Matrix4x4 vpMatrix = ReadViewProjectionMatrix(screenWidth, screenHeight);

    // Step 3: Get local player's pawn address (to skip ourselves)
    uintptr_t localPawnAddr = GetLocalPawnAddress();

    // Step 4: Read PersistentLevel
    uintptr_t persistentLevel = m_memory->Read<uintptr_t>(
        m_worldAddress + offsets::UWorld::PersistentLevel
    );
    if (persistentLevel == 0) return;

    // Step 5: Read actor array pointer and count
    uintptr_t actorArray = m_memory->Read<uintptr_t>(
        persistentLevel + offsets::ULevel::ActorArray
    );
    int actorCount = m_memory->Read<int32_t>(
        persistentLevel + offsets::ULevel::ActorCount
    );

    if (actorArray == 0 || actorCount <= 0) return;
    actorCount = std::min(actorCount, MAX_ACTORS);

    // Step 6: Read all actor pointers at once (batch read for performance)
    std::vector<uintptr_t> actorPtrs(actorCount);
    if (!m_memory->ReadRaw(actorArray, actorPtrs.data(), actorCount * sizeof(uintptr_t))) {
        return;
    }

    // Step 7: Process each actor
    for (int i = 0; i < actorCount; ++i) {
        uintptr_t actorAddr = actorPtrs[i];
        if (actorAddr == 0) continue;

        PlayerEntity player;
        if (!ReadPlayerEntity(actorAddr, player)) continue;

        // Check if this is the local player
        player.isLocalPlayer = (actorAddr == localPawnAddr);

        if (player.isLocalPlayer) {
            m_localPlayer = player;
            continue;  // Don't add local player to the render list
        }

        // Skip dead players
        if (player.isDead) continue;

        // Calculate distance from local player
        player.distance = math::UnrealUnitsToMeters(
            m_localPlayer.position.Distance(player.position)
        );

        // World-to-screen projection
        player.isOnScreen = math::WorldToScreen(
            player.position, vpMatrix,
            screenWidth, screenHeight,
            player.screenPos
        );

        // Calculate bounding box
        if (player.isOnScreen) {
            CalculateBoundingBox(player, vpMatrix, screenWidth, screenHeight);
        }

        // Read bone positions (for skeleton ESP)
        uintptr_t meshComp = m_memory->Read<uintptr_t>(
            actorAddr + offsets::AFortPlayerPawn::Mesh
        );
        if (meshComp != 0 && player.isOnScreen) {
            ReadBones(meshComp, player, vpMatrix, screenWidth, screenHeight);
        }

        m_players.push_back(std::move(player));
    }
}

// ============================================================================
//  Read a single player entity's data
// ============================================================================

bool EntityList::ReadPlayerEntity(uintptr_t actorAddress, PlayerEntity& out) {
    out.address = actorAddress;

    // Read root component for position
    uintptr_t rootComponent = m_memory->Read<uintptr_t>(
        actorAddress + offsets::AActor::RootComponent
    );
    if (rootComponent == 0) return false;

    // Read world position
    out.position = m_memory->Read<math::Vector3>(
        rootComponent + offsets::USceneComponent::RelativeLocation
    );

    // Sanity check — position should be non-zero for valid players
    if (out.position.IsZero()) return false;

    // Read health & shield
    out.health = m_memory->Read<float>(
        actorAddress + offsets::AFortPlayerPawn::CurrentHealth
    );
    out.maxHealth = m_memory->Read<float>(
        actorAddress + offsets::AFortPlayerPawn::MaxHealth
    );
    out.shield = m_memory->Read<float>(
        actorAddress + offsets::AFortPlayerPawn::CurrentShield
    );
    out.maxShield = m_memory->Read<float>(
        actorAddress + offsets::AFortPlayerPawn::MaxShield
    );

    // Validate — if max health is 0 or negative, probably not a player pawn
    if (out.maxHealth <= 0) return false;

    // Read death state
    out.isDead = m_memory->Read<bool>(
        actorAddress + offsets::AFortPlayerPawn::bIsDying
    );

    // Read player state for name and team
    uintptr_t playerState = m_memory->Read<uintptr_t>(
        actorAddress + offsets::AFortPlayerPawn::PlayerState
    );

    if (playerState != 0) {
        // Team index
        out.teamIndex = m_memory->Read<uint8_t>(
            playerState + offsets::APlayerState::TeamIndex
        );

        // Player name (FString)
        out.name = m_memory->ReadFString(
            playerState + offsets::APlayerState::PlayerNamePrivate
        );
    }

    return true;
}

// ============================================================================
//  Read the view-projection matrix from the camera system
// ============================================================================

math::Matrix4x4 EntityList::ReadViewProjectionMatrix(int screenWidth, int screenHeight) {
    if (m_worldAddress == 0) return math::Matrix4x4::Identity();

    // Navigate: UWorld → GameInstance → LocalPlayers[0] → PlayerController → CameraManager
    uintptr_t gameInstance = m_memory->Read<uintptr_t>(
        m_worldAddress + offsets::UWorld::OwningGameInstance
    );
    if (gameInstance == 0) return math::Matrix4x4::Identity();

    // LocalPlayers is a TArray — read the data pointer, then first element
    uintptr_t localPlayersArray = m_memory->Read<uintptr_t>(
        gameInstance + offsets::UGameInstance::LocalPlayers
    );
    if (localPlayersArray == 0) return math::Matrix4x4::Identity();

    uintptr_t localPlayer = m_memory->Read<uintptr_t>(localPlayersArray);
    if (localPlayer == 0) return math::Matrix4x4::Identity();

    uintptr_t playerController = m_memory->Read<uintptr_t>(
        localPlayer + offsets::ULocalPlayer::PlayerController
    );
    if (playerController == 0) return math::Matrix4x4::Identity();

    uintptr_t cameraManager = m_memory->Read<uintptr_t>(
        playerController + offsets::APlayerController::PlayerCameraManager
    );
    if (cameraManager == 0) return math::Matrix4x4::Identity();

    // Read camera POV from the CameraCache
    uintptr_t cacheBase = cameraManager + offsets::APlayerCameraManager::CameraCache 
                        + offsets::APlayerCameraManager::CachePOV;

    math::Vector3 cameraLocation = m_memory->Read<math::Vector3>(
        cacheBase + offsets::APlayerCameraManager::POV_Location
    );

    math::FRotator cameraRotation = m_memory->Read<math::FRotator>(
        cacheBase + offsets::APlayerCameraManager::POV_Rotation
    );

    float fov = m_memory->Read<float>(
        cacheBase + offsets::APlayerCameraManager::POV_FOV
    );

    // Sanity — default FOV if read failed
    if (fov <= 0 || fov > 170) fov = 80.0f;

    // Build the combined view-projection matrix
    return math::BuildViewProjectionMatrix(
        cameraLocation, cameraRotation, fov,
        static_cast<float>(screenWidth),
        static_cast<float>(screenHeight)
    );
}

// ============================================================================
//  Get local player's pawn address
// ============================================================================

uintptr_t EntityList::GetLocalPawnAddress() {
    if (m_worldAddress == 0) return 0;

    uintptr_t gameInstance = m_memory->Read<uintptr_t>(
        m_worldAddress + offsets::UWorld::OwningGameInstance
    );
    if (gameInstance == 0) return 0;

    uintptr_t localPlayersArray = m_memory->Read<uintptr_t>(
        gameInstance + offsets::UGameInstance::LocalPlayers
    );
    if (localPlayersArray == 0) return 0;

    uintptr_t localPlayer = m_memory->Read<uintptr_t>(localPlayersArray);
    if (localPlayer == 0) return 0;

    uintptr_t playerController = m_memory->Read<uintptr_t>(
        localPlayer + offsets::ULocalPlayer::PlayerController
    );
    if (playerController == 0) return 0;

    return m_memory->Read<uintptr_t>(
        playerController + offsets::APlayerController::AcknowledgedPawn
    );
}

// ============================================================================
//  Read bone positions for skeleton ESP
// ============================================================================

void EntityList::ReadBones(
    uintptr_t meshComponent,
    PlayerEntity& entity,
    const math::Matrix4x4& vpMatrix,
    int screenW, int screenH
) {
    // Read ComponentToWorld transform
    math::FTransform componentToWorld = m_memory->Read<math::FTransform>(
        meshComponent + offsets::USkeletalMeshComponent::ComponentToWorld
    );

    // Read bone array pointer
    uintptr_t boneArray = m_memory->Read<uintptr_t>(
        meshComponent + offsets::USkeletalMeshComponent::BoneArray
    );
    if (boneArray == 0) return;

    int boneCount = m_memory->Read<int32_t>(
        meshComponent + offsets::USkeletalMeshComponent::BoneCount
    );
    if (boneCount <= 0 || boneCount > 500) return;

    // Bones we care about for skeleton ESP
    const int bonesToRead[] = {
        static_cast<int>(offsets::EBoneIndex::Head),
        static_cast<int>(offsets::EBoneIndex::Neck),
        static_cast<int>(offsets::EBoneIndex::Chest),
        static_cast<int>(offsets::EBoneIndex::Pelvis),
        static_cast<int>(offsets::EBoneIndex::LeftShoulder),
        static_cast<int>(offsets::EBoneIndex::LeftElbow),
        static_cast<int>(offsets::EBoneIndex::LeftHand),
        static_cast<int>(offsets::EBoneIndex::RightShoulder),
        static_cast<int>(offsets::EBoneIndex::RightElbow),
        static_cast<int>(offsets::EBoneIndex::RightHand),
        static_cast<int>(offsets::EBoneIndex::LeftHip),
        static_cast<int>(offsets::EBoneIndex::LeftKnee),
        static_cast<int>(offsets::EBoneIndex::LeftFoot),
        static_cast<int>(offsets::EBoneIndex::RightHip),
        static_cast<int>(offsets::EBoneIndex::RightKnee),
        static_cast<int>(offsets::EBoneIndex::RightFoot),
    };

    entity.hasBones = true;

    for (int boneIndex : bonesToRead) {
        if (boneIndex >= boneCount) continue;

        // Each bone is an FTransform (sizeof = 48 bytes with padding, but UE uses 0x30 = 48)
        // Actually FTransform in UE is: Quat(16) + Translation(16, padded) + Scale(16, padded) = 48
        // But stored as: float4 rotation, float4 translation (with w padding), float4 scale
        constexpr size_t TRANSFORM_SIZE = 0x30;  // 48 bytes per FTransform

        math::FTransform boneTransform = m_memory->Read<math::FTransform>(
            boneArray + boneIndex * TRANSFORM_SIZE
        );

        // Transform bone to world space
        math::Vector3 boneWorldPos = math::GetBoneWorldPosition(boneTransform, componentToWorld);

        // Project to screen
        math::Vector2 boneScreenPos;
        if (math::WorldToScreen(boneWorldPos, vpMatrix, screenW, screenH, boneScreenPos)) {
            entity.boneScreenPositions[boneIndex] = boneScreenPos;
        }
    }
}

// ============================================================================
//  Calculate 2D bounding box from player position
// ============================================================================

void EntityList::CalculateBoundingBox(
    PlayerEntity& entity,
    const math::Matrix4x4& vpMatrix,
    int screenW, int screenH
) {
    // Project head position (position + height offset)
    math::Vector3 headPos = entity.position;
    headPos.z += PLAYER_HEIGHT_UU;

    // Project foot position
    math::Vector3 footPos = entity.position;
    footPos.z -= 10.0f;  // Slight offset below feet

    math::Vector2 headScreen, footScreen;
    
    bool headVisible = math::WorldToScreen(headPos, vpMatrix, screenW, screenH, headScreen);
    bool footVisible = math::WorldToScreen(footPos, vpMatrix, screenW, screenH, footScreen);

    if (!headVisible || !footVisible) {
        // Use the main screen position as fallback
        entity.box.top = entity.screenPos.y - 40;
        entity.box.bottom = entity.screenPos.y + 40;
        entity.box.left = entity.screenPos.x - 20;
        entity.box.right = entity.screenPos.x + 20;
        entity.box.height = 80;
        entity.box.width = 40;
        return;
    }

    // Box height from head to foot projection
    entity.box.height = std::abs(footScreen.y - headScreen.y);
    entity.box.width = entity.box.height * 0.45f;  // Width ≈ 45% of height

    // Center the box horizontally on the player
    float centerX = (headScreen.x + footScreen.x) / 2.0f;

    entity.box.top = headScreen.y - 5.0f;  // Padding above head
    entity.box.bottom = footScreen.y + 2.0f;
    entity.box.left = centerX - entity.box.width / 2.0f;
    entity.box.right = centerX + entity.box.width / 2.0f;

    // Recalculate actual dimensions
    entity.box.height = entity.box.bottom - entity.box.top;
    entity.box.width = entity.box.right - entity.box.left;
}

} // namespace entity
