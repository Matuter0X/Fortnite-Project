// ============================================================================
//  UE5 INTERNAL STRUCTURES — The reflection system's skeleton
//  
//  Unreal Engine stores metadata about every class, property, and object
//  in memory. These structures mirror that layout so we can read them
//  from the target process and reconstruct the SDK.
//
//  This is how we pull offsets without any external tools.
// ============================================================================

#pragma once

#include <cstdint>

namespace ue {

// ============================================================================
//  FName — Unreal's interned string system
//  
//  Every name in the engine is stored in a global pool (GNames).
//  FName is just an index into that pool + an instance number.
// ============================================================================

struct FNameEntryId {
    uint32_t Value = 0;

    // In UE5, the index is split:
    //   BlockIndex = Value >> 16  (which block in the name pool)
    //   OffsetInBlock = Value & 0xFFFF  (offset within that block)
    uint32_t GetBlockIndex() const { return Value >> 16; }
    uint32_t GetOffsetInBlock() const { return Value & 0xFFFF; }
};

struct FName {
    FNameEntryId ComparisonIndex;  // Index into the name pool
    uint32_t Number = 0;           // Instance number (for uniqueness)
};

// FNameEntry layout in memory (UE5 FNamePool)
// The name pool uses a chunked allocation scheme
struct FNameEntry {
    // Header contains flags + length
    // Bit layout: [bIsWide:1][Len:15] (UE4) or varies in UE5
    uint16_t Header;

    bool IsWide() const { return (Header & 1) != 0; }
    int GetLength() const { return Header >> 1; }
    // Followed by char[Length] or wchar_t[Length] depending on IsWide
};

// ============================================================================
//  GNames Pool — UE5 uses FNamePool with blocks
//  
//  Layout:
//    FNamePool {
//      Lock;
//      CurrentBlock: uint32;
//      CurrentByteCursor: uint32;
//      Blocks[FNameMaxBlocks]: uint8*;  // array of pointers to blocks
//    }
//  
//  Each block is a contiguous allocation. Names are packed sequentially.
// ============================================================================

constexpr int FNameMaxBlocks = 8192;
constexpr int FNameBlockOffsetBits = 16;
constexpr int FNameBlockSize = 1 << FNameBlockOffsetBits;  // 65536 bytes per block

// ============================================================================
//  UObject — base of everything in Unreal
// ============================================================================

struct UObjectBase {
    uintptr_t VTablePtr;          // 0x00 — Virtual function table
    int32_t ObjectFlags;          // 0x08 — EObjectFlags
    int32_t InternalIndex;        // 0x0C — Index in GObjects array
    uintptr_t ClassPrivate;       // 0x10 — UClass* (pointer to this object's class)
    FName NamePrivate;            // 0x18 — Object name
    uintptr_t OuterPrivate;       // 0x20 — Outer object (package/owner)
};

static_assert(sizeof(FName) == 8, "FName size mismatch");

// ============================================================================
//  UField — base for reflection chain (UE4 style, may differ in UE5)
// ============================================================================

struct UField {
    UObjectBase Base;              // Inherited from UObject
    uintptr_t Next;                // 0x28 — Next UField in the linked list
};

// ============================================================================
//  FField — UE5's new field system (replaced UField for properties)
//  
//  In UE5, properties use FField instead of UField.
//  FField has a different layout than UObject.
// ============================================================================

struct FField {
    uintptr_t VTablePtr;          // 0x00
    uintptr_t ClassPrivate;       // 0x08 — FFieldClass*
    uintptr_t Owner;              // 0x10 — Owner (UStruct or FField)
    uintptr_t Next;               // 0x18 — Next FField in chain
    FName NamePrivate;            // 0x20 — Field name
    int32_t FlagsPrivate;         // 0x28 — EObjectFlags
};

// ============================================================================
//  FProperty — UE5's property descriptor (replaces UProperty)
//  
//  Contains the critical Offset_Internal field — this is the offset
//  of the member variable within its owning struct/class.
// ============================================================================

struct FProperty {
    FField Base;                   // Inherited from FField

    int32_t ArrayDim;              // 0x30 — Array dimension (1 for non-arrays)
    int32_t ElementSize;           // 0x34 — Size of one element
    uint64_t PropertyFlags;        // 0x38 — EPropertyFlags
    uint16_t RepIndex;             // 0x40
    uint8_t  BlueprintRepCond;     // 0x42
    uint8_t  Pad43;                // 0x43
    int32_t Offset_Internal;       // 0x44 — THE MONEY SHOT: offset within the struct
    // ... more fields follow but we don't need them
};

// ============================================================================
//  UStruct — base for classes and structs
// ============================================================================

struct UStruct {
    UObjectBase Base;
    uint8_t Pad28[0x10];           // UField::Next + padding
    uintptr_t SuperStruct;         // 0x38 (varies) — Parent class
    uintptr_t Children;            // 0x40 (varies) — First UField child (legacy)
    uintptr_t ChildProperties;     // 0x48 (varies) — First FField child (UE5 properties)
    int32_t PropertiesSize;        // 0x50 (varies) — Total struct size
    // ... more fields
};

// ============================================================================
//  UClass — full class descriptor
//  Extends UStruct with class-specific data
// ============================================================================

// We don't need the full UClass layout — UStruct gives us what we need
// (SuperStruct, ChildProperties, PropertiesSize)

// ============================================================================
//  GObjects — Global object array
//  
//  UE5 uses FChunkedFixedUObjectArray:
//    Objects: UObjectBase**[MaxChunks]  — array of chunk pointers
//    Each chunk is UObjectBase*[ChunkSize]
// ============================================================================

constexpr int GObjectsChunkSize = 64 * 1024;  // 65536 objects per chunk
constexpr int GObjectsMaxChunks = 2 * 1024;   // Up to ~134M objects total

struct FUObjectItem {
    uintptr_t Object;              // 0x00 — UObjectBase*
    int32_t Flags;                 // 0x08
    int32_t ClusterRootIndex;      // 0x0C
    int32_t SerialNumber;          // 0x10
    int32_t Pad;                   // 0x14
    // Total size: 0x18 (24 bytes)
};

static_assert(sizeof(FUObjectItem) == 24, "FUObjectItem size mismatch");

// ============================================================================
//  Offsets within these structures that we need to know
//  These are ENGINE-level offsets (not game-level) and are very stable
//  across UE5 versions. But they can vary between major versions.
// ============================================================================

namespace EngineOffsets {
    // UObjectBase
    constexpr int UObject_ClassPrivate = 0x10;
    constexpr int UObject_NamePrivate = 0x18;
    constexpr int UObject_OuterPrivate = 0x20;

    // UStruct (offsets from start of object)
    // These vary between UE versions — these are UE5 estimates
    constexpr int UStruct_SuperStruct = 0x40;
    constexpr int UStruct_Children = 0x48;
    constexpr int UStruct_ChildProperties = 0x50;
    constexpr int UStruct_PropertiesSize = 0x58;

    // FField
    constexpr int FField_Next = 0x18;
    constexpr int FField_Name = 0x20;

    // FProperty  
    constexpr int FProperty_Offset = 0x44;
    constexpr int FProperty_ElementSize = 0x34;

    // FNamePool
    // GNames usually points to an FNamePool instance
    // Blocks start at offset ~0x10 after the lock + counters
    constexpr int FNamePool_Blocks = 0x10;
}

} // namespace ue
