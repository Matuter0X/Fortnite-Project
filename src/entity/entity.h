// ============================================================================
//  ENTITY SYSTEM — Player data extraction from game memory
//  Read the world, find the players, know where they hide.
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "math.h"

// Forward declaration
namespace mem { class ProcessMemory; }

namespace entity {

// ============================================================================
//  PlayerEntity — everything we know about a player
// ============================================================================

struct PlayerEntity {
    uintptr_t address = 0;              // Actor pointer in game memory
    math::Vector3 position;              // World position (Unreal units)
    math::Vector2 screenPos;             // Projected screen position (pixels)
    bool isOnScreen = false;             // Whether W2S succeeded
    
    // Vitals
    float health = 0;
    float maxHealth = 100;
    float shield = 0;
    float maxShield = 100;
    
    // Identity
    int teamIndex = 0;
    std::string name;
    
    // Spatial
    float distance = 0;                  // Distance from local player (meters)
    bool isDead = false;
    bool isLocalPlayer = false;
    
    // Bones (for skeleton ESP)
    std::unordered_map<int, math::Vector2> boneScreenPositions;
    bool hasBones = false;
    
    // 2D bounding box (screen-space pixels)
    struct BoundingBox {
        float top = 0, bottom = 0;
        float left = 0, right = 0;
        float height = 0, width = 0;
    } box;

    /// Health as percentage [0.0 - 1.0]
    float HealthPercent() const {
        return (maxHealth > 0) ? (health / maxHealth) : 0;
    }

    /// Shield as percentage [0.0 - 1.0]
    float ShieldPercent() const {
        return (maxShield > 0) ? (shield / maxShield) : 0;
    }
};

// ============================================================================
//  EntityList — manages reading and updating all player entities
// ============================================================================

class EntityList {
public:
    EntityList() = default;

    /// Initialize with a reference to the memory reader
    void Initialize(const mem::ProcessMemory* memory);

    /// Update all entities from game memory — call once per frame
    /// screenWidth/Height are needed for world-to-screen projection
    void Update(int screenWidth, int screenHeight);

    /// Get the current list of visible enemy/team players
    const std::vector<PlayerEntity>& GetPlayers() const { return m_players; }

    /// Get local player data
    const PlayerEntity& GetLocalPlayer() const { return m_localPlayer; }

    /// Check if the entity list has valid data
    bool IsValid() const { return m_worldAddress != 0; }

private:
    const mem::ProcessMemory* m_memory = nullptr;

    std::vector<PlayerEntity> m_players;
    PlayerEntity m_localPlayer;

    uintptr_t m_worldAddress = 0;

    /// Read the view-projection matrix from the camera manager
    /// Navigates: UWorld → GameInstance → LocalPlayers[0] → PlayerController 
    ///            → CameraManager → CameraCache → POV
    math::Matrix4x4 ReadViewProjectionMatrix(int screenWidth, int screenHeight);

    /// Read a single actor and populate a PlayerEntity struct
    /// Returns false if the actor is invalid or not a player
    bool ReadPlayerEntity(uintptr_t actorAddress, PlayerEntity& out);

    /// Read bone positions for skeleton ESP
    void ReadBones(
        uintptr_t meshComponent, 
        PlayerEntity& entity,
        const math::Matrix4x4& vpMatrix,
        int screenW, int screenH
    );

    /// Calculate the 2D bounding box from head/foot positions
    void CalculateBoundingBox(
        PlayerEntity& entity,
        const math::Matrix4x4& vpMatrix,
        int screenW, int screenH
    );

    /// Get the local player's pawn address for identification
    uintptr_t GetLocalPawnAddress();
};

} // namespace entity
