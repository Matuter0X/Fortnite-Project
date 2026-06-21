// ============================================================================
//  UE5 SDK DUMPER — Automatic offset extraction
//  
//  Uses Unreal Engine's built-in reflection system to read class metadata
//  directly from game memory. No external tools needed.
//
//  How it works:
//  1. Sig-scan for GNames (name pool) and GObjects (object array)
//  2. Build a name resolution cache
//  3. Walk GObjects to find target classes (AFortPlayerPawn, etc.)
//  4. For each class, walk its FProperty chain
//  5. Read each property's name and Offset_Internal
//  6. Export to offsets.json
//
//  This is the same approach tools like Dumper-7 and UEDumper use.
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

// Forward declarations
namespace mem { class ProcessMemory; }

namespace dumper {

// ============================================================================
//  Types
// ============================================================================

/// A single property extracted from a UE5 class
struct DumpedProperty {
    std::string name;           // Property name (e.g., "CurrentHealth")
    int32_t offset = 0;         // Offset within the struct
    int32_t size = 0;           // Element size in bytes
    std::string typeName;       // Property type (e.g., "FloatProperty")
    std::string className;      // Owning class name
};

/// A dumped class with all its properties
struct DumpedClass {
    std::string name;                       // Class name (e.g., "AFortPlayerPawn")
    std::string superClassName;             // Parent class name
    int32_t structSize = 0;                 // Total struct size
    uintptr_t address = 0;                  // Address in game memory
    std::vector<DumpedProperty> properties; // All properties
};

/// Progress callback for long operations
using ProgressCallback = std::function<void(const std::string& status, int percent)>;

// ============================================================================
//  Dumper class
// ============================================================================

class SDKDumper {
public:
    SDKDumper() = default;

    /// Initialize the dumper with a memory reader
    /// Automatically sig-scans for GNames and GObjects
    bool Initialize(const mem::ProcessMemory& memory);

    /// Dump a specific class by name
    /// Returns nullopt if the class wasn't found
    DumpedClass DumpClass(const std::string& className);

    /// Dump all classes we care about for the ESP and auto-update offsets.json
    /// This is the main "do everything" function
    bool DumpAndUpdateOffsets(
        const std::string& outputPath = "offsets.json",
        ProgressCallback progress = nullptr
    );

    /// Dump the full SDK to a text file (all classes, all properties)
    /// Useful for research and finding new offsets
    bool DumpFullSDK(
        const std::string& outputPath = "sdk_dump.txt",
        ProgressCallback progress = nullptr
    );

    /// Resolve an FName to its string representation
    std::string ResolveName(uint32_t nameIndex);

    /// Check if the dumper is ready (GNames + GObjects found)
    bool IsReady() const { return m_gnamesAddress != 0 && m_gobjectsAddress != 0; }

    /// Get addresses for debugging
    uintptr_t GetGNamesAddress() const { return m_gnamesAddress; }
    uintptr_t GetGObjectsAddress() const { return m_gobjectsAddress; }

private:
    const mem::ProcessMemory* m_memory = nullptr;

    uintptr_t m_gnamesAddress = 0;     // Address of FNamePool
    uintptr_t m_gobjectsAddress = 0;   // Address of FUObjectArray
    int32_t m_objectCount = 0;          // Number of objects in GObjects

    // Name cache — avoid re-reading the same name
    std::unordered_map<uint32_t, std::string> m_nameCache;

    // ====== GNames resolution ======

    /// Find GNames via signature scanning
    uintptr_t FindGNames();

    /// Read a name entry from the FNamePool
    std::string ReadNameEntry(uint32_t nameIndex);

    // ====== GObjects traversal ======

    /// Find GObjects via signature scanning
    uintptr_t FindGObjects();

    /// Read a UObject pointer from GObjects by index
    uintptr_t ReadObjectByIndex(int32_t index);

    /// Get the total object count
    int32_t ReadObjectCount();

    // ====== Class/Property walking ======

    /// Find a UClass object by name (e.g., "AFortPlayerPawn")
    uintptr_t FindClassByName(const std::string& className);

    /// Read the name of a UObject at the given address
    std::string ReadObjectName(uintptr_t objectAddress);

    /// Read the class name of a UObject
    std::string ReadObjectClassName(uintptr_t objectAddress);

    /// Walk the FProperty chain of a UStruct and collect all properties
    std::vector<DumpedProperty> WalkProperties(
        uintptr_t structAddress,
        const std::string& className
    );

    /// Read super class pointer from a UStruct
    uintptr_t ReadSuperStruct(uintptr_t structAddress);

    /// Read the ChildProperties pointer from a UStruct
    uintptr_t ReadChildProperties(uintptr_t structAddress);

    /// Read struct size from a UStruct
    int32_t ReadStructSize(uintptr_t structAddress);

    // ====== Offset file generation ======

    /// Convert dumped classes to our offsets.json format
    bool WriteOffsetsFile(
        const std::string& path,
        const std::vector<DumpedClass>& classes
    );
};

} // namespace dumper
