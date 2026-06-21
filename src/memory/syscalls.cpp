// ============================================================================
//  SYSCALL LAYER — Implementation
//  
//  Strategy: Read ntdll.dll from DISK (not the in-memory copy which EAC may
//  have hooked). Parse the PE export table, find the Nt* functions, extract
//  their syscall numbers from the `mov eax, <number>` instruction, then
//  build tiny executable stubs that call `syscall` directly.
//
//  This bypasses any user-mode hooks placed on ntdll functions.
//  It does NOT bypass kernel-level monitoring — EAC sees the syscall
//  regardless. But it removes one detection vector.
// ============================================================================

#include "syscalls.h"
#include <string>
#include <fstream>
#include <vector>

namespace syscall {

// ============================================================================
//  Internal state
// ============================================================================

namespace detail {
    // Syscall numbers — resolved at runtime from ntdll on disk
    static DWORD sysNtReadVirtualMemory = 0;
    static DWORD sysNtOpenProcess = 0;
    static DWORD sysNtClose = 0;
    
    static bool initialized = false;

    // ========================================================================
    //  Executable stub for syscall invocation
    //  
    //  MSVC x64 doesn't allow inline assembly, so we build a small
    //  executable code buffer in memory and cast it to a function pointer.
    //  
    //  The stub pattern (x64):
    //    mov r10, rcx          ; 49 89 CA (syscall convention: r10 = first arg)
    //    mov eax, <number>     ; B8 xx xx xx xx
    //    syscall               ; 0F 05
    //    ret                   ; C3
    // ========================================================================

    struct SyscallStub {
        BYTE code[16];
        
        void Build(DWORD syscallNumber) {
            // mov r10, rcx
            code[0] = 0x49;
            code[1] = 0x89;
            code[2] = 0xCA;
            
            // mov eax, <syscall_number>
            code[3] = 0xB8;
            *reinterpret_cast<DWORD*>(&code[4]) = syscallNumber;
            
            // syscall
            code[8] = 0x0F;
            code[9] = 0x05;
            
            // ret
            code[10] = 0xC3;
            
            // Padding (nop)
            code[11] = 0x90;
            code[12] = 0x90;
            code[13] = 0x90;
            code[14] = 0x90;
            code[15] = 0x90;
        }
    };

    // Executable memory for our stubs
    static void* executablePage = nullptr;
    static SyscallStub* stubReadVM = nullptr;
    static SyscallStub* stubOpenProcess = nullptr;
    static SyscallStub* stubClose = nullptr;

    // ========================================================================
    //  PE Parsing — read ntdll from disk and extract syscall numbers
    // ========================================================================

    // Read a file from disk into a byte buffer
    static std::vector<BYTE> ReadFileBytes(const std::wstring& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<BYTE> buffer(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        return buffer;
    }

    // Convert RVA to file offset using PE section headers
    static DWORD RvaToOffset(const std::vector<BYTE>& pe, DWORD rva) {
        auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
        auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dosHeader->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(ntHeaders);
        
        for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
            if (rva >= section[i].VirtualAddress &&
                rva < section[i].VirtualAddress + section[i].Misc.VirtualSize) {
                return rva - section[i].VirtualAddress + section[i].PointerToRawData;
            }
        }
        return 0;
    }

    // Find a syscall number for a named export in the ntdll PE file
    // The pattern at the start of each Nt* function is:
    //   4C 8B D1        mov r10, rcx
    //   B8 XX XX XX XX  mov eax, <syscall_number>
    static DWORD FindSyscallNumber(
        const std::vector<BYTE>& pe,
        const char* functionName
    ) {
        auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
        auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dosHeader->e_lfanew);
        
        // Get export directory
        auto exportDirRva = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if (exportDirRva == 0) return 0;
        
        auto exportDirOffset = RvaToOffset(pe, exportDirRva);
        auto exportDir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(pe.data() + exportDirOffset);
        
        // Get export tables
        auto namesOffset = RvaToOffset(pe, exportDir->AddressOfNames);
        auto ordinalsOffset = RvaToOffset(pe, exportDir->AddressOfNameOrdinals);
        auto functionsOffset = RvaToOffset(pe, exportDir->AddressOfFunctions);
        
        auto nameRvas = reinterpret_cast<const DWORD*>(pe.data() + namesOffset);
        auto ordinals = reinterpret_cast<const WORD*>(pe.data() + ordinalsOffset);
        auto funcRvas = reinterpret_cast<const DWORD*>(pe.data() + functionsOffset);
        
        // Search for our function
        for (DWORD i = 0; i < exportDir->NumberOfNames; ++i) {
            auto nameOffset = RvaToOffset(pe, nameRvas[i]);
            auto name = reinterpret_cast<const char*>(pe.data() + nameOffset);
            
            if (strcmp(name, functionName) == 0) {
                // Found it — get the function's file offset
                auto funcOffset = RvaToOffset(pe, funcRvas[ordinals[i]]);
                auto funcBytes = pe.data() + funcOffset;
                
                // Verify the pattern: 4C 8B D1 B8 xx xx xx xx
                if (funcBytes[0] == 0x4C && funcBytes[1] == 0x8B && funcBytes[2] == 0xD1 &&
                    funcBytes[3] == 0xB8) {
                    // The syscall number is at offset 4 (after mov eax,)
                    return *reinterpret_cast<const DWORD*>(&funcBytes[4]);
                }
                
                // Some functions have a different pattern — try searching forward
                for (int offset = 0; offset < 32; ++offset) {
                    if (funcBytes[offset] == 0xB8 && 
                        funcBytes[offset + 5] == 0x0F && funcBytes[offset + 6] == 0x05) {
                        return *reinterpret_cast<const DWORD*>(&funcBytes[offset + 1]);
                    }
                }
                
                return 0; // Found the function but couldn't extract syscall number
            }
        }
        
        return 0; // Function not found
    }

    // Allocate executable memory for syscall stubs
    static bool AllocateStubs() {
        // Allocate a page of executable memory
        executablePage = VirtualAlloc(
            nullptr, 
            4096, 
            MEM_COMMIT | MEM_RESERVE, 
            PAGE_EXECUTE_READWRITE
        );
        
        if (!executablePage) return false;
        
        // Place stubs in the executable page
        stubReadVM = reinterpret_cast<SyscallStub*>(executablePage);
        stubOpenProcess = stubReadVM + 1;
        stubClose = stubOpenProcess + 1;
        
        return true;
    }

} // namespace detail

// ============================================================================
//  Public API implementation
// ============================================================================

bool Initialize() {
    if (detail::initialized) return true;
    
    // Step 1: Read ntdll.dll from disk
    // Using the system directory path to get the clean, unhookd copy
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);
    
    std::wstring ntdllPath = std::wstring(systemDir) + L"\\ntdll.dll";
    auto ntdllBytes = detail::ReadFileBytes(ntdllPath);
    
    if (ntdllBytes.empty()) {
        return false; // Can't read ntdll — something is very wrong
    }
    
    // Step 2: Extract syscall numbers
    detail::sysNtReadVirtualMemory = detail::FindSyscallNumber(ntdllBytes, "NtReadVirtualMemory");
    detail::sysNtOpenProcess = detail::FindSyscallNumber(ntdllBytes, "NtOpenProcess");
    detail::sysNtClose = detail::FindSyscallNumber(ntdllBytes, "NtClose");
    
    // Verify we got the critical ones
    if (detail::sysNtReadVirtualMemory == 0) {
        return false;
    }
    
    // Step 3: Allocate executable memory and build stubs
    if (!detail::AllocateStubs()) {
        return false;
    }
    
    detail::stubReadVM->Build(detail::sysNtReadVirtualMemory);
    
    if (detail::sysNtOpenProcess != 0) {
        detail::stubOpenProcess->Build(detail::sysNtOpenProcess);
    }
    
    if (detail::sysNtClose != 0) {
        detail::stubClose->Build(detail::sysNtClose);
    }
    
    // Flush instruction cache to ensure the CPU sees our new code
    FlushInstructionCache(GetCurrentProcess(), detail::executablePage, 4096);
    
    detail::initialized = true;
    return true;
}

bool IsAvailable() {
    return detail::initialized;
}

NTSTATUS NtReadVirtualMemory(
    HANDLE  processHandle,
    PVOID   baseAddress,
    PVOID   buffer,
    SIZE_T  size,
    PSIZE_T bytesRead
) {
    if (!detail::initialized || !detail::stubReadVM) {
        return STATUS_UNSUCCESSFUL;
    }
    
    // Cast our stub to the NtReadVirtualMemory function signature and call it
    using fn_NtReadVirtualMemory = NTSTATUS(NTAPI*)(
        HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T
    );
    
    auto func = reinterpret_cast<fn_NtReadVirtualMemory>(detail::stubReadVM);
    return func(processHandle, baseAddress, buffer, size, bytesRead);
}

NTSTATUS NtOpenProcess(
    PHANDLE            processHandle,
    ACCESS_MASK        desiredAccess,
    POBJECT_ATTRIBUTES objectAttributes,
    PCLIENT_ID         clientId
) {
    if (!detail::initialized || !detail::stubOpenProcess || detail::sysNtOpenProcess == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    
    using fn_NtOpenProcess = NTSTATUS(NTAPI*)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID
    );
    
    auto func = reinterpret_cast<fn_NtOpenProcess>(detail::stubOpenProcess);
    return func(processHandle, desiredAccess, objectAttributes, clientId);
}

NTSTATUS NtClose(HANDLE handle) {
    if (!detail::initialized || !detail::stubClose || detail::sysNtClose == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    
    using fn_NtClose = NTSTATUS(NTAPI*)(HANDLE);
    
    auto func = reinterpret_cast<fn_NtClose>(detail::stubClose);
    return func(handle);
}

} // namespace syscall
