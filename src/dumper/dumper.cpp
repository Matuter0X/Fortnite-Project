// ============================================================================
//  UE5 SDK DUMPER — Implementation
//  
//  The crown jewel. This reads Unreal Engine's reflection metadata directly
//  from Fortnite's memory to extract class property offsets automatically.
//
//  No external dumper tools needed. No community offset threads.
//  Just attach → scan → dump → profit.
// ============================================================================

#include "dumper.h"
#include "ue_structures.h"
#include "../memory/memory.h"
#include "../offsets/offsets.h"
#include "../offsets/scanner.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace dumper {

// ============================================================================
//  Initialization
// ============================================================================

bool SDKDumper::Initialize(const mem::ProcessMemory& memory) {
    m_memory = &memory;
    m_nameCache.clear();

    // Step 1: Find GNames
    m_gnamesAddress = FindGNames();
    if (m_gnamesAddress == 0) {
        return false;
    }

    // Step 2: Find GObjects
    m_gobjectsAddress = FindGObjects();
    if (m_gobjectsAddress == 0) {
        return false;
    }

    // Step 3: Read object count
    m_objectCount = ReadObjectCount();

    return IsReady();
}

// ============================================================================
//  GNames — Finding and reading the name pool
// ============================================================================

uintptr_t SDKDumper::FindGNames() {
    if (!m_memory) return 0;

    // UE5 GNames signatures — these access the global FNamePool
    const char* signatures[] = {
        // Pattern: lea rcx, [rip+??]; call FName::Init
        "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 0D",
        // Pattern: lea rdx, [rip+??] (GNamePool reference)
        "48 8D 15 ?? ?? ?? ?? EB ?? 48 8D 0D",
        // Pattern: mov rax, [rip+??] (direct GNames access)
        "48 8B 05 ?? ?? ?? ?? 48 85 C0 75 ?? B9 ?? ?? ?? ?? E8",
    };

    for (const char* sig : signatures) {
        auto result = scanner::FindPatternInModule(sig);
        if (result.found) {
            // Resolve RIP-relative address (offset at position 3, instruction length 7)
            int32_t relOffset = m_memory->Read<int32_t>(result.address + 3);
            uintptr_t addr = result.address + 7 + relOffset;
            
            if (addr != 0) {
                // Validate: try to read a known name (index 0 should be "None")
                std::string testName = ReadNameEntry(0);
                if (testName == "None" || testName == "none") {
                    return addr;
                }
                
                // If direct address didn't work, try as a pointer to the pool
                uintptr_t poolAddr = m_memory->Read<uintptr_t>(addr);
                if (poolAddr != 0) {
                    uintptr_t savedAddr = m_gnamesAddress;
                    m_gnamesAddress = poolAddr;
                    testName = ReadNameEntry(0);
                    if (testName == "None" || testName == "none") {
                        return poolAddr;
                    }
                    m_gnamesAddress = savedAddr;
                }
            }
        }
    }

    return 0;
}

std::string SDKDumper::ReadNameEntry(uint32_t nameIndex) {
    if (m_gnamesAddress == 0 || !m_memory) return "";

    // Check cache first
    auto it = m_nameCache.find(nameIndex);
    if (it != m_nameCache.end()) return it->second;

    // UE5 FNamePool block-based lookup
    uint32_t blockIndex = nameIndex >> ue::FNameBlockOffsetBits;
    uint32_t offsetInBlock = nameIndex & (ue::FNameBlockSize - 1);

    if (blockIndex >= ue::FNameMaxBlocks) return "";

    // Read the block pointer from FNamePool.Blocks[blockIndex]
    uintptr_t blockPtr = m_memory->Read<uintptr_t>(
        m_gnamesAddress + ue::EngineOffsets::FNamePool_Blocks + blockIndex * sizeof(uintptr_t)
    );

    if (blockPtr == 0) return "";

    // Read the entry header at the offset within the block
    // Each entry has a 2-byte header followed by the string data
    // In UE5, entries are stride-aligned (2 byte header + string, aligned to 2 bytes)
    uintptr_t entryAddr = blockPtr + offsetInBlock * 2;  // Stride is 2 bytes in UE5

    uint16_t header = m_memory->Read<uint16_t>(entryAddr);
    
    bool isWide = (header & 1) != 0;
    int length = header >> 1;

    if (length <= 0 || length > 1024) return "";

    std::string name;
    if (!isWide) {
        // ASCII name
        name = m_memory->ReadString(entryAddr + 2, length);
    } else {
        // Wide name — read as wstring then convert
        std::wstring wname = m_memory->ReadWString(entryAddr + 2, length);
        name.reserve(wname.size());
        for (wchar_t wc : wname) {
            if (wc == 0) break;
            name += static_cast<char>(wc & 0x7F);
        }
    }

    // Truncate at actual length
    if (name.size() > static_cast<size_t>(length)) {
        name.resize(length);
    }

    // Cache it
    m_nameCache[nameIndex] = name;
    return name;
}

std::string SDKDumper::ResolveName(uint32_t nameIndex) {
    return ReadNameEntry(nameIndex);
}

// ============================================================================
//  GObjects — Finding and traversing the object array
// ============================================================================

uintptr_t SDKDumper::FindGObjects() {
    if (!m_memory) return 0;

    // UE5 GObjects (FUObjectArray) signatures
    const char* signatures[] = {
        // Pattern: lea rax, [rip+??] (GUObjectArray reference)
        "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1",
        // Pattern: Common GObjects access
        "48 8B 05 ?? ?? ?? ?? 48 63 CA 48 8B 0C C8",
        // Pattern: Another GObjects access
        "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B D6 48 8B CF",
    };

    for (const char* sig : signatures) {
        auto result = scanner::FindPatternInModule(sig);
        if (result.found) {
            int32_t relOffset = m_memory->Read<int32_t>(result.address + 3);
            uintptr_t addr = result.address + 7 + relOffset;
            
            if (addr != 0) {
                // Could be direct address or pointer — try both
                // Check if there are valid objects at this address
                uintptr_t firstChunkPtr = m_memory->Read<uintptr_t>(addr);
                if (firstChunkPtr != 0) {
                    // Try to read the first object
                    ue::FUObjectItem firstItem = m_memory->Read<ue::FUObjectItem>(firstChunkPtr);
                    if (firstItem.Object != 0) {
                        return addr;
                    }
                }
                
                // Try as the FUObjectArray struct itself
                // FUObjectArray layout: ObjObjects (ptr), MaxElements, NumElements
                uintptr_t objectsPtr = m_memory->Read<uintptr_t>(addr);
                if (objectsPtr != 0) {
                    uintptr_t chunkPtr = m_memory->Read<uintptr_t>(objectsPtr);
                    if (chunkPtr != 0) {
                        return addr;
                    }
                }
            }
        }
    }

    return 0;
}

uintptr_t SDKDumper::ReadObjectByIndex(int32_t index) {
    if (m_gobjectsAddress == 0 || !m_memory) return 0;
    if (index < 0 || index >= m_objectCount) return 0;

    // FChunkedFixedUObjectArray layout:
    // +0x00: UObjectBase*** Objects (pointer to array of chunk pointers)
    // +0x08: ... (pre-allocated, etc.)
    // +0x10: int32 MaxElements
    // +0x14: int32 NumElements

    uintptr_t chunksPtr = m_memory->Read<uintptr_t>(m_gobjectsAddress);
    if (chunksPtr == 0) return 0;

    int32_t chunkIndex = index / ue::GObjectsChunkSize;
    int32_t withinChunk = index % ue::GObjectsChunkSize;

    // Read the chunk pointer
    uintptr_t chunkPtr = m_memory->Read<uintptr_t>(
        chunksPtr + chunkIndex * sizeof(uintptr_t)
    );
    if (chunkPtr == 0) return 0;

    // Read the FUObjectItem at the correct position within the chunk
    ue::FUObjectItem item = m_memory->Read<ue::FUObjectItem>(
        chunkPtr + withinChunk * sizeof(ue::FUObjectItem)
    );

    return item.Object;
}

int32_t SDKDumper::ReadObjectCount() {
    if (m_gobjectsAddress == 0 || !m_memory) return 0;

    // NumElements is typically at offset 0x14 in FChunkedFixedUObjectArray
    // But the exact layout varies. Try common offsets.
    int32_t count = m_memory->Read<int32_t>(m_gobjectsAddress + 0x14);
    
    // Sanity check
    if (count <= 0 || count > 5000000) {
        // Try alternate offset
        count = m_memory->Read<int32_t>(m_gobjectsAddress + 0x18);
    }
    if (count <= 0 || count > 5000000) {
        count = m_memory->Read<int32_t>(m_gobjectsAddress + 0x0C);
    }

    return (count > 0 && count < 5000000) ? count : 0;
}

// ============================================================================
//  Object name/class reading
// ============================================================================

std::string SDKDumper::ReadObjectName(uintptr_t objectAddress) {
    if (objectAddress == 0 || !m_memory) return "";

    ue::FName name = m_memory->Read<ue::FName>(
        objectAddress + ue::EngineOffsets::UObject_NamePrivate
    );

    return ResolveName(name.ComparisonIndex.Value);
}

std::string SDKDumper::ReadObjectClassName(uintptr_t objectAddress) {
    if (objectAddress == 0 || !m_memory) return "";

    uintptr_t classPtr = m_memory->Read<uintptr_t>(
        objectAddress + ue::EngineOffsets::UObject_ClassPrivate
    );

    return ReadObjectName(classPtr);
}

// ============================================================================
//  Class finding — search GObjects for a class by name
// ============================================================================

uintptr_t SDKDumper::FindClassByName(const std::string& className) {
    if (!IsReady()) return 0;

    // Strip common prefixes for flexible matching
    std::string searchName = className;
    
    for (int32_t i = 0; i < m_objectCount; ++i) {
        uintptr_t obj = ReadObjectByIndex(i);
        if (obj == 0) continue;

        // Check if this object IS a class (its class should be "Class")
        std::string objClassName = ReadObjectClassName(obj);
        if (objClassName != "Class" && objClassName != "ScriptStruct") continue;

        // Check if its name matches what we're looking for
        std::string objName = ReadObjectName(obj);
        if (objName == searchName || objName == className) {
            return obj;
        }
    }

    return 0;
}

// ============================================================================
//  Property walking — extract all properties from a class/struct
// ============================================================================

std::vector<DumpedProperty> SDKDumper::WalkProperties(
    uintptr_t structAddress,
    const std::string& className
) {
    std::vector<DumpedProperty> properties;
    if (structAddress == 0 || !m_memory) return properties;

    // Read ChildProperties — head of the FField linked list
    uintptr_t fieldPtr = ReadChildProperties(structAddress);

    while (fieldPtr != 0) {
        DumpedProperty prop;
        prop.className = className;

        // Read property name
        ue::FName fieldName = m_memory->Read<ue::FName>(
            fieldPtr + ue::EngineOffsets::FField_Name
        );
        prop.name = ResolveName(fieldName.ComparisonIndex.Value);

        // Read offset and size
        prop.offset = m_memory->Read<int32_t>(
            fieldPtr + ue::EngineOffsets::FProperty_Offset
        );
        prop.size = m_memory->Read<int32_t>(
            fieldPtr + ue::EngineOffsets::FProperty_ElementSize
        );

        // Read property type (from FField's ClassPrivate)
        uintptr_t fieldClassPtr = m_memory->Read<uintptr_t>(fieldPtr + 0x08);
        if (fieldClassPtr != 0) {
            // FFieldClass has a Name FName at a known offset
            ue::FName typeName = m_memory->Read<ue::FName>(fieldClassPtr + 0x00);
            prop.typeName = ResolveName(typeName.ComparisonIndex.Value);
        }

        // Only add properties with valid names and reasonable offsets
        if (!prop.name.empty() && prop.offset >= 0 && prop.offset < 0x10000) {
            properties.push_back(std::move(prop));
        }

        // Move to next field in the linked list
        fieldPtr = m_memory->Read<uintptr_t>(
            fieldPtr + ue::EngineOffsets::FField_Next
        );
    }

    return properties;
}

uintptr_t SDKDumper::ReadSuperStruct(uintptr_t structAddress) {
    return m_memory->Read<uintptr_t>(
        structAddress + ue::EngineOffsets::UStruct_SuperStruct
    );
}

uintptr_t SDKDumper::ReadChildProperties(uintptr_t structAddress) {
    return m_memory->Read<uintptr_t>(
        structAddress + ue::EngineOffsets::UStruct_ChildProperties
    );
}

int32_t SDKDumper::ReadStructSize(uintptr_t structAddress) {
    return m_memory->Read<int32_t>(
        structAddress + ue::EngineOffsets::UStruct_PropertiesSize
    );
}

// ============================================================================
//  Dump a single class
// ============================================================================

DumpedClass SDKDumper::DumpClass(const std::string& className) {
    DumpedClass result;
    result.name = className;

    uintptr_t classAddr = FindClassByName(className);
    if (classAddr == 0) return result;

    result.address = classAddr;
    result.structSize = ReadStructSize(classAddr);

    // Read super class name
    uintptr_t superAddr = ReadSuperStruct(classAddr);
    if (superAddr != 0) {
        result.superClassName = ReadObjectName(superAddr);
    }

    // Walk properties for THIS class
    result.properties = WalkProperties(classAddr, className);

    // Also walk inherited properties from parent classes
    uintptr_t parentAddr = superAddr;
    while (parentAddr != 0) {
        std::string parentName = ReadObjectName(parentAddr);
        auto parentProps = WalkProperties(parentAddr, parentName);
        
        // Prepend parent properties (they come first in memory layout)
        result.properties.insert(
            result.properties.begin(),
            parentProps.begin(),
            parentProps.end()
        );

        parentAddr = ReadSuperStruct(parentAddr);
    }

    // Sort by offset for readability
    std::sort(result.properties.begin(), result.properties.end(),
        [](const DumpedProperty& a, const DumpedProperty& b) {
            return a.offset < b.offset;
        }
    );

    return result;
}

// ============================================================================
//  Dump all ESP-relevant classes and update offsets.json
// ============================================================================

bool SDKDumper::DumpAndUpdateOffsets(
    const std::string& outputPath,
    ProgressCallback progress
) {
    if (!IsReady()) return false;

    // Classes we need for the ESP
    const char* targetClasses[] = {
        "World",                    // UWorld (named "World" internally)
        "Level",                    // ULevel
        "Actor",                    // AActor
        "SceneComponent",           // USceneComponent
        "PlayerState",              // APlayerState
        "FortPlayerPawn",           // AFortPlayerPawn (Fortnite-specific)
        "FortPlayerPawnAthena",     // Battle Royale specific pawn
        "PlayerCameraManager",      // APlayerCameraManager
        "SkeletalMeshComponent",    // USkeletalMeshComponent
        "GameInstance",             // UGameInstance
        "LocalPlayer",              // ULocalPlayer
        "PlayerController",         // APlayerController
        "FortPlayerController",     // AFortPlayerController
    };

    std::vector<DumpedClass> dumpedClasses;
    int total = sizeof(targetClasses) / sizeof(targetClasses[0]);

    for (int i = 0; i < total; ++i) {
        if (progress) {
            int pct = (i * 100) / total;
            progress(std::string("Dumping ") + targetClasses[i] + "...", pct);
        }

        auto dumped = DumpClass(targetClasses[i]);
        if (!dumped.properties.empty()) {
            dumpedClasses.push_back(std::move(dumped));
        }
    }

    if (progress) {
        progress("Writing offsets file...", 90);
    }

    // Now map the dumped properties to our offset structures
    auto findProp = [&](const std::string& className, const std::string& propName) -> int32_t {
        for (auto& cls : dumpedClasses) {
            // Fuzzy class matching (e.g., "World" matches "UWorld")
            if (cls.name == className || 
                cls.name == className.substr(1) ||  // Strip U/A/F prefix
                ("U" + cls.name) == className ||
                ("A" + cls.name) == className ||
                ("F" + cls.name) == className) {
                
                for (auto& prop : cls.properties) {
                    if (prop.name == propName) {
                        return prop.offset;
                    }
                }
            }
        }
        return -1;  // Not found
    };

    // Update the offset structures with found values
    int32_t val;

    // UWorld
    if ((val = findProp("World", "PersistentLevel")) >= 0) offsets::UWorld::PersistentLevel = val;
    if ((val = findProp("World", "OwningGameInstance")) >= 0) offsets::UWorld::OwningGameInstance = val;
    if ((val = findProp("World", "GameState")) >= 0) offsets::UWorld::GameState = val;

    // UGameInstance
    if ((val = findProp("GameInstance", "LocalPlayers")) >= 0) offsets::UGameInstance::LocalPlayers = val;

    // ULocalPlayer
    if ((val = findProp("LocalPlayer", "PlayerController")) >= 0) offsets::ULocalPlayer::PlayerController = val;

    // APlayerController
    if ((val = findProp("PlayerController", "PlayerCameraManager")) >= 0) offsets::APlayerController::PlayerCameraManager = val;
    if ((val = findProp("PlayerController", "AcknowledgedPawn")) >= 0) offsets::APlayerController::AcknowledgedPawn = val;

    // ULevel
    if ((val = findProp("Level", "Actors")) >= 0) {
        offsets::ULevel::ActorArray = val;
        offsets::ULevel::ActorCount = val + 8;  // TArray: Data(8) + Count(4)
    }

    // AActor
    if ((val = findProp("Actor", "RootComponent")) >= 0) offsets::AActor::RootComponent = val;

    // USceneComponent
    if ((val = findProp("SceneComponent", "RelativeLocation")) >= 0) offsets::USceneComponent::RelativeLocation = val;

    // APlayerState
    if ((val = findProp("PlayerState", "PawnPrivate")) >= 0) offsets::APlayerState::PawnPrivate = val;
    if ((val = findProp("PlayerState", "PlayerNamePrivate")) >= 0) offsets::APlayerState::PlayerNamePrivate = val;
    if ((val = findProp("PlayerState", "TeamIndex")) >= 0) offsets::APlayerState::TeamIndex = val;

    // AFortPlayerPawn / FortPlayerPawnAthena
    if ((val = findProp("FortPlayerPawn", "CurrentHealth")) >= 0) offsets::AFortPlayerPawn::CurrentHealth = val;
    if ((val = findProp("FortPlayerPawn", "MaxHealth")) >= 0) offsets::AFortPlayerPawn::MaxHealth = val;
    if ((val = findProp("FortPlayerPawn", "CurrentShield")) >= 0) offsets::AFortPlayerPawn::CurrentShield = val;
    if ((val = findProp("FortPlayerPawn", "MaxShield")) >= 0) offsets::AFortPlayerPawn::MaxShield = val;
    if ((val = findProp("FortPlayerPawn", "Mesh")) >= 0) offsets::AFortPlayerPawn::Mesh = val;
    if ((val = findProp("FortPlayerPawn", "PlayerState")) >= 0) offsets::AFortPlayerPawn::PlayerState = val;
    if ((val = findProp("FortPlayerPawn", "bIsDying")) >= 0) offsets::AFortPlayerPawn::bIsDying = val;

    // Also check FortPlayerPawnAthena (BR-specific subclass)
    if ((val = findProp("FortPlayerPawnAthena", "CurrentHealth")) >= 0) offsets::AFortPlayerPawn::CurrentHealth = val;
    if ((val = findProp("FortPlayerPawnAthena", "CurrentShield")) >= 0) offsets::AFortPlayerPawn::CurrentShield = val;

    // APlayerCameraManager
    if ((val = findProp("PlayerCameraManager", "CameraCache")) >= 0) offsets::APlayerCameraManager::CameraCache = val;

    // USkeletalMeshComponent
    if ((val = findProp("SkeletalMeshComponent", "BoneSpaceTransforms")) >= 0) offsets::USkeletalMeshComponent::BoneArray = val;
    if ((val = findProp("SkeletalMeshComponent", "ComponentToWorld")) >= 0) offsets::USkeletalMeshComponent::ComponentToWorld = val;

    // Save to file
    bool saved = offsets::SaveToFile(outputPath);

    if (progress) {
        progress("Done!", 100);
    }

    return saved;
}

// ============================================================================
//  Full SDK dump — everything to a text file
// ============================================================================

bool SDKDumper::DumpFullSDK(
    const std::string& outputPath,
    ProgressCallback progress
) {
    if (!IsReady()) return false;

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    file << "// ============================================\n";
    file << "// UE5 SDK DUMP — Auto-generated\n";
    file << "// GNames:   0x" << std::hex << m_gnamesAddress << "\n";
    file << "// GObjects: 0x" << std::hex << m_gobjectsAddress << "\n";
    file << "// Objects:  " << std::dec << m_objectCount << "\n";
    file << "// ============================================\n\n";

    int classCount = 0;

    for (int32_t i = 0; i < m_objectCount; ++i) {
        if (i % 10000 == 0 && progress) {
            int pct = (i * 100) / m_objectCount;
            progress("Scanning objects... " + std::to_string(i) + "/" + std::to_string(m_objectCount), pct);
        }

        uintptr_t obj = ReadObjectByIndex(i);
        if (obj == 0) continue;

        // Check if this is a Class or ScriptStruct
        std::string objClassName = ReadObjectClassName(obj);
        if (objClassName != "Class" && objClassName != "ScriptStruct") continue;

        std::string name = ReadObjectName(obj);
        if (name.empty()) continue;

        // Get struct size
        int32_t structSize = ReadStructSize(obj);

        // Get super class
        uintptr_t superAddr = ReadSuperStruct(obj);
        std::string superName = (superAddr != 0) ? ReadObjectName(superAddr) : "None";

        // Dump header
        file << "\n// ---- " << objClassName << ": " << name 
             << " (Size: 0x" << std::hex << structSize << std::dec
             << ", Super: " << superName << ") ----\n";

        // Walk properties
        auto props = WalkProperties(obj, name);
        for (auto& prop : props) {
            file << "    [0x" << std::hex << std::setw(4) << std::setfill('0') << prop.offset 
                 << "] " << std::setfill(' ') << std::setw(40) << std::left << prop.name 
                 << " // " << prop.typeName << " (size: " << std::dec << prop.size << ")\n";
        }

        classCount++;
    }

    file << "\n// Total classes dumped: " << classCount << "\n";

    if (progress) {
        progress("SDK dump complete! " + std::to_string(classCount) + " classes", 100);
    }

    return true;
}

// ============================================================================
//  Offset file writing (delegates to offsets::SaveToFile)
// ============================================================================

bool SDKDumper::WriteOffsetsFile(
    const std::string& path,
    const std::vector<DumpedClass>& classes
) {
    // We already updated the offset structures in DumpAndUpdateOffsets,
    // so just save them
    return offsets::SaveToFile(path);
}

} // namespace dumper
