// ============================================================================
//  OFFSET DEFINITIONS — JSON serialization
//  Load and save offset values so you don't have to recompile every patch.
// ============================================================================

#include "offsets.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace offsets {

// ============================================================================
//  Minimal JSON parser — because we don't need nlohmann for 30 values
//  
//  Supports flat objects with hex string values:
//    { "section.key": "0x1234", ... }
// ============================================================================

namespace json {

    // Trim whitespace
    static std::string Trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        auto end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }

    // Parse a hex string like "0x1234" or "4660" to uintptr_t
    static uintptr_t ParseHex(const std::string& s) {
        std::string trimmed = Trim(s);
        // Remove quotes if present
        if (trimmed.front() == '"') trimmed = trimmed.substr(1);
        if (trimmed.back() == '"') trimmed = trimmed.substr(0, trimmed.size() - 1);
        trimmed = Trim(trimmed);

        try {
            if (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
                return std::stoull(trimmed, nullptr, 16);
            }
            return std::stoull(trimmed);
        } catch (...) {
            return 0;
        }
    }

    // Format a value as hex string
    static std::string ToHex(uintptr_t value) {
        std::ostringstream ss;
        ss << "0x" << std::hex << std::uppercase << value;
        return ss.str();
    }

    // Parse a flat JSON-like config file into key-value pairs
    // Format: { "key": "value", ... } — supports nested with dot notation
    static std::unordered_map<std::string, std::string> ParseFile(const std::string& path) {
        std::unordered_map<std::string, std::string> result;

        std::ifstream file(path);
        if (!file.is_open()) return result;

        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        // Simple state machine parser for our flat JSON format
        std::string currentSection;
        size_t pos = 0;
        
        while (pos < content.size()) {
            // Skip whitespace and structural chars
            while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' || 
                   content[pos] == '\r' || content[pos] == '\n' || content[pos] == ',' || 
                   content[pos] == '{' || content[pos] == '}')) {
                pos++;
            }
            if (pos >= content.size()) break;

            // Read key (expect quoted string)
            if (content[pos] == '"') {
                pos++; // skip opening quote
                size_t keyEnd = content.find('"', pos);
                if (keyEnd == std::string::npos) break;
                std::string key = content.substr(pos, keyEnd - pos);
                pos = keyEnd + 1;

                // Skip to colon
                while (pos < content.size() && content[pos] != ':') pos++;
                pos++; // skip colon

                // Skip whitespace
                while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;

                if (pos >= content.size()) break;

                // Check if value is an object (section) or a string value
                if (content[pos] == '{') {
                    currentSection = key;
                    pos++; // skip opening brace
                } else if (content[pos] == '"') {
                    pos++; // skip opening quote
                    size_t valEnd = content.find('"', pos);
                    if (valEnd == std::string::npos) break;
                    std::string value = content.substr(pos, valEnd - pos);
                    pos = valEnd + 1;

                    // Store with section prefix
                    std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
                    result[fullKey] = value;
                }
            } else {
                pos++; // skip unknown char
            }
        }

        return result;
    }

} // namespace json

// ============================================================================
//  Offset mapping — connects JSON keys to our static variables
// ============================================================================

struct OffsetEntry {
    std::string key;
    uintptr_t* target;
};

static std::vector<OffsetEntry> GetOffsetMap() {
    return {
        // UWorld
        {"UWorld.GWorld",            &UWorld::GWorld},
        {"UWorld.PersistentLevel",   &UWorld::PersistentLevel},
        {"UWorld.OwningGameInstance", &UWorld::OwningGameInstance},
        {"UWorld.GameState",         &UWorld::GameState},
        
        // UGameInstance
        {"UGameInstance.LocalPlayers", &UGameInstance::LocalPlayers},
        
        // ULocalPlayer
        {"ULocalPlayer.PlayerController", &ULocalPlayer::PlayerController},
        
        // APlayerController
        {"APlayerController.PlayerCameraManager", &APlayerController::PlayerCameraManager},
        {"APlayerController.AcknowledgedPawn",    &APlayerController::AcknowledgedPawn},
        
        // ULevel
        {"ULevel.ActorArray", &ULevel::ActorArray},
        {"ULevel.ActorCount", &ULevel::ActorCount},
        
        // AActor
        {"AActor.RootComponent", &AActor::RootComponent},
        
        // USceneComponent
        {"USceneComponent.RelativeLocation", &USceneComponent::RelativeLocation},
        
        // APlayerState
        {"APlayerState.PawnPrivate",       &APlayerState::PawnPrivate},
        {"APlayerState.PlayerNamePrivate", &APlayerState::PlayerNamePrivate},
        {"APlayerState.TeamIndex",         &APlayerState::TeamIndex},
        
        // AFortPlayerPawn
        {"AFortPlayerPawn.PlayerState",   &AFortPlayerPawn::PlayerState},
        {"AFortPlayerPawn.Mesh",          &AFortPlayerPawn::Mesh},
        {"AFortPlayerPawn.CurrentHealth", &AFortPlayerPawn::CurrentHealth},
        {"AFortPlayerPawn.MaxHealth",     &AFortPlayerPawn::MaxHealth},
        {"AFortPlayerPawn.CurrentShield", &AFortPlayerPawn::CurrentShield},
        {"AFortPlayerPawn.MaxShield",     &AFortPlayerPawn::MaxShield},
        {"AFortPlayerPawn.bIsDying",      &AFortPlayerPawn::bIsDying},
        
        // Camera
        {"APlayerCameraManager.CameraCache", &APlayerCameraManager::CameraCache},
        {"APlayerCameraManager.CachePOV",    &APlayerCameraManager::CachePOV},
        {"APlayerCameraManager.POV_Location", &APlayerCameraManager::POV_Location},
        {"APlayerCameraManager.POV_Rotation", &APlayerCameraManager::POV_Rotation},
        {"APlayerCameraManager.POV_FOV",      &APlayerCameraManager::POV_FOV},
        
        // Skeletal Mesh
        {"USkeletalMeshComponent.BoneArray",        &USkeletalMeshComponent::BoneArray},
        {"USkeletalMeshComponent.BoneCount",        &USkeletalMeshComponent::BoneCount},
        {"USkeletalMeshComponent.ComponentToWorld",  &USkeletalMeshComponent::ComponentToWorld},
    };
}

// ============================================================================
//  Load / Save
// ============================================================================

bool LoadFromFile(const std::string& path) {
    auto kvMap = json::ParseFile(path);
    if (kvMap.empty()) return false;

    auto offsetMap = GetOffsetMap();
    int loaded = 0;

    for (auto& entry : offsetMap) {
        auto it = kvMap.find(entry.key);
        if (it != kvMap.end()) {
            *entry.target = json::ParseHex(it->second);
            loaded++;
        }
    }

    return (loaded > 0);
}

bool SaveToFile(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    auto offsetMap = GetOffsetMap();

    // Group by section (everything before the '.')
    std::string currentSection;
    bool firstSection = true;
    bool firstEntry = true;

    file << "{\n";

    for (size_t i = 0; i < offsetMap.size(); ++i) {
        auto& entry = offsetMap[i];
        
        // Extract section name
        size_t dot = entry.key.find('.');
        std::string section = (dot != std::string::npos) ? entry.key.substr(0, dot) : "";
        std::string key = (dot != std::string::npos) ? entry.key.substr(dot + 1) : entry.key;

        if (section != currentSection) {
            // Close previous section
            if (!firstSection) {
                file << "\n    },\n";
            }
            
            // Open new section
            file << "    \"" << section << "\": {\n";
            currentSection = section;
            firstSection = false;
            firstEntry = true;
        }

        if (!firstEntry) {
            file << ",\n";
        }

        file << "        \"" << key << "\": \"" << json::ToHex(*entry.target) << "\"";
        firstEntry = false;
    }

    // Close last section and root
    if (!firstSection) {
        file << "\n    }\n";
    }
    file << "}\n";

    return true;
}

} // namespace offsets
