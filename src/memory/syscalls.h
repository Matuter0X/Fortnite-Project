// ============================================================================
//  SYSCALL LAYER — Direct NT syscalls to bypass ntdll hooks
//  Because if EAC is watching the front door, we go through the wall.
// ============================================================================

#pragma once

#include <Windows.h>

// NT status codes we care about
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

// NT structures that aren't in the standard headers
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

namespace syscall {

// ============================================================================
//  Public API
// ============================================================================

/// Initialize the syscall layer by resolving syscall numbers from ntdll.dll
/// on DISK (not the in-memory version which may be hooked).
/// Returns true if all critical syscall numbers were resolved.
bool Initialize();

/// Direct syscall wrapper for NtReadVirtualMemory
/// Reads memory from a target process without going through the hooked ntdll
NTSTATUS NtReadVirtualMemory(
    HANDLE  processHandle,
    PVOID   baseAddress,
    PVOID   buffer,
    SIZE_T  size,
    PSIZE_T bytesRead
);

/// Direct syscall wrapper for NtOpenProcess
NTSTATUS NtOpenProcess(
    PHANDLE            processHandle,
    ACCESS_MASK        desiredAccess,
    POBJECT_ATTRIBUTES objectAttributes,
    PCLIENT_ID         clientId
);

/// Direct syscall wrapper for NtClose
NTSTATUS NtClose(HANDLE handle);

/// Check if the syscall layer is available
bool IsAvailable();

} // namespace syscall
