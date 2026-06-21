// ============================================================================
//  MEMORY READER — Implementation
//  The hands that reach into Fortnite's memory and pull out player data.
// ============================================================================

#include "memory.h"
#include "syscalls.h"
#include <Psapi.h>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
#ifndef NTAPI
#define NTAPI __stdcall
#endif

namespace mem {

namespace {

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid = {};
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);

    const bool ok = GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    CloseHandle(token);
    return ok;
}

} // namespace

bool ProcessMemory::IsRunningElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

namespace {

static bool ModuleNameMatches(const std::wstring& moduleName, const std::wstring& targetName) {
    if (targetName.empty()) return true;
    if (_wcsicmp(moduleName.c_str(), targetName.c_str()) == 0) return true;

    auto stripExe = [](std::wstring name) {
        if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) {
            name.resize(name.size() - 4);
        }
        return name;
    };

    return _wcsicmp(stripExe(moduleName).c_str(), stripExe(targetName).c_str()) == 0;
}

} // namespace

// ============================================================================
//  Lifecycle
// ============================================================================

ProcessMemory::~ProcessMemory() {
    if (m_processHandle && m_processHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
}

// ============================================================================
//  Attachment
// ============================================================================

bool ProcessMemory::Attach(const std::wstring& windowTitle) {
    HWND hwnd = FindGameWindow(windowTitle);
    if (!hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    if (!OpenProcessHandle(pid)) return false;
    return ResolveModuleInfo();
}

bool ProcessMemory::AttachByName(const std::wstring& processName) {
    static bool debugPrivilege = EnableDebugPrivilege();
    (void)debugPrivilege;

    m_lastAttachDiagnostic.clear();

    DWORD pid = FindProcessId(processName);
    if (pid == 0) {
        std::wstring diag = L"Process not found: " + processName;
        auto fortniteProcs = ListProcessesContaining(L"Fortnite");
        if (!fortniteProcs.empty()) {
            diag += L"\nFortnite-related processes running:";
            for (const auto& name : fortniteProcs) {
                diag += L"\n  - " + name;
            }
            diag += L"\nUpdate config.json -> target.process if the game exe name differs.";
        } else {
            diag += L"\nNo process with 'Fortnite' in the name is running.";
            diag += L"\nLaunch the game past the lobby, not just the Epic launcher.";
        }
        SetAttachDiagnostic(diag);
        return false;
    }

    if (!OpenProcessHandle(pid)) {
        SetAttachDiagnostic(
            L"OpenProcess failed for PID " + std::to_wstring(pid) +
            L" (error " + std::to_wstring(GetLastError()) +
            L"). Run as Administrator."
        );
        return false;
    }

    m_moduleName = processName;
    if (!ResolveModuleInfo(processName)) {
        std::wstring diag = L"Opened PID " + std::to_wstring(pid) +
            L" but could not resolve the module base address.";
        diag += ProcessMemory::IsRunningElevated()
            ? L"\nRunning as Administrator: yes"
            : L"\nRunning as Administrator: NO — right-click Command Prompt -> Run as administrator";
        diag += L"\n";
        diag += m_lastAttachDiagnostic;
        SetAttachDiagnostic(diag);
        return false;
    }

    m_lastAttachDiagnostic.clear();
    return true;
}

// ============================================================================
//  Module resolution
// ============================================================================

uintptr_t ProcessMemory::GetModuleBase(const std::wstring& moduleName) const {
    if (m_processId == 0 || !m_processHandle) return 0;

    const std::wstring& targetName = moduleName.empty() ? m_moduleName : moduleName;
    uintptr_t firstModule = 0;

    auto trySnapshot = [&](DWORD flags) -> uintptr_t {
        HANDLE snapshot = CreateToolhelp32Snapshot(flags, m_processId);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        MODULEENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        uintptr_t matched = 0;

        if (Module32FirstW(snapshot, &entry)) {
            do {
                const uintptr_t base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                if (firstModule == 0) firstModule = base;

                if (ModuleNameMatches(entry.szModule, targetName)) {
                    matched = base;
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return matched;
    };

    uintptr_t base = trySnapshot(TH32CS_SNAPMODULE);
    if (base != 0) return base;

    base = trySnapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32);
    if (base != 0) return base;

    HMODULE modules[1024] = {};
    DWORD bytesNeeded = 0;

    auto tryEnum = [&](auto fn) -> uintptr_t {
        if (!fn(m_processHandle, modules, sizeof(modules), &bytesNeeded)) {
            return 0;
        }

        const size_t moduleCount = bytesNeeded / sizeof(HMODULE);
        uintptr_t matched = 0;

        for (size_t i = 0; i < moduleCount; ++i) {
            if (firstModule == 0) {
                firstModule = reinterpret_cast<uintptr_t>(modules[i]);
            }

            wchar_t name[MAX_PATH] = {};
            if (GetModuleBaseNameW(m_processHandle, modules[i], name, MAX_PATH) == 0) {
                continue;
            }

            if (ModuleNameMatches(name, targetName)) {
                matched = reinterpret_cast<uintptr_t>(modules[i]);
                break;
            }
        }

        return matched;
    };

    base = tryEnum([](auto h, auto m, auto sz, auto* needed) {
        return EnumProcessModulesEx(
            h, m, sz, needed, LIST_MODULES_64BIT) != FALSE;
    });
    if (base != 0) return base;

    base = tryEnum([](auto h, auto m, auto sz, auto* needed) {
        return EnumProcessModules(h, m, sz, needed) != FALSE;
    });
    if (base != 0) return base;

    // Main executable is always the first module in Psapi/Toolhelp order
    return firstModule;
}

size_t ProcessMemory::GetModuleSize(const std::wstring& moduleName) const {
    if (m_processId == 0 || !m_processHandle) return 0;

    const std::wstring& targetName = moduleName.empty() ? m_moduleName : moduleName;

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        m_processId
    );

    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry = {};
        entry.dwSize = sizeof(entry);

        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (targetName.empty() ||
                    _wcsicmp(entry.szModule, targetName.c_str()) == 0) {
                    const size_t size = entry.modBaseSize;
                    CloseHandle(snapshot);
                    return size;
                }
            } while (Module32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    HMODULE modules[1024] = {};
    DWORD bytesNeeded = 0;
    if (!EnumProcessModules(m_processHandle, modules, sizeof(modules), &bytesNeeded)) {
        return 0;
    }

    const size_t moduleCount = bytesNeeded / sizeof(HMODULE);
    for (size_t i = 0; i < moduleCount; ++i) {
        wchar_t name[MAX_PATH] = {};
        if (GetModuleBaseNameW(m_processHandle, modules[i], name, MAX_PATH) == 0) {
            continue;
        }

        if (targetName.empty() || _wcsicmp(name, targetName.c_str()) == 0) {
            MODULEINFO info = {};
            if (GetModuleInformation(m_processHandle, modules[i], &info, sizeof(info))) {
                return info.SizeOfImage;
            }
            return 0;
        }
    }

    return 0;
}

// ============================================================================
//  Memory reading
// ============================================================================

bool ProcessMemory::ReadRaw(uintptr_t address, void* buffer, size_t size) const {
    if (!m_processHandle || !buffer || size == 0) return false;

    // Try direct syscalls first (bypasses ntdll hooks)
    if (m_useSyscalls && syscall::IsAvailable()) {
        SIZE_T bytesRead = 0;
        NTSTATUS status = syscall::NtReadVirtualMemory(
            m_processHandle,
            reinterpret_cast<PVOID>(address),
            buffer,
            size,
            &bytesRead
        );
        if (status == STATUS_SUCCESS && bytesRead == size) {
            return true;
        }
    }

    // Fallback: standard ReadProcessMemory
    SIZE_T bytesRead = 0;
    BOOL success = ReadProcessMemory(
        m_processHandle,
        reinterpret_cast<LPCVOID>(address),
        buffer,
        size,
        &bytesRead
    );

    return (success && bytesRead == size);
}

uintptr_t ProcessMemory::ReadPointerChain(
    uintptr_t base, 
    const std::vector<uintptr_t>& offsets
) const {
    uintptr_t current = base;

    for (size_t i = 0; i < offsets.size(); ++i) {
        // For all but the last offset, dereference the pointer
        if (i < offsets.size() - 1) {
            current = Read<uintptr_t>(current + offsets[i]);
            if (current == 0) return 0;  // Null pointer in chain
        } else {
            // Last offset: just add it (don't dereference)
            current += offsets[i];
        }
    }

    return current;
}

std::string ProcessMemory::ReadString(uintptr_t address, size_t maxLength) const {
    if (address == 0) return "";

    std::vector<char> buffer(maxLength + 1, '\0');
    ReadRaw(address, buffer.data(), maxLength);

    // Ensure null termination
    buffer[maxLength] = '\0';

    return std::string(buffer.data());
}

std::wstring ProcessMemory::ReadWString(uintptr_t address, size_t maxLength) const {
    if (address == 0) return L"";

    std::vector<wchar_t> buffer(maxLength + 1, L'\0');
    ReadRaw(address, buffer.data(), maxLength * sizeof(wchar_t));

    buffer[maxLength] = L'\0';

    return std::wstring(buffer.data());
}

std::string ProcessMemory::ReadFString(uintptr_t address) const {
    if (address == 0) return "";

    // FString layout in UE:
    // +0x00: TCHAR* Data (pointer to wide char array)
    // +0x08: int32 ArrayNum (string length including null terminator)
    // +0x0C: int32 ArrayMax (allocated capacity)

    uintptr_t dataPtr = Read<uintptr_t>(address);
    int32_t length = Read<int32_t>(address + 0x08);

    if (dataPtr == 0 || length <= 0 || length > 256) return "";

    // Read the wide string data
    std::wstring wstr = ReadWString(dataPtr, static_cast<size_t>(length));

    // Convert to ASCII (lossy, but player names are usually ASCII-ish)
    std::string result;
    result.reserve(wstr.size());
    for (wchar_t wc : wstr) {
        if (wc == L'\0') break;
        result += static_cast<char>(wc & 0x7F);
    }

    return result;
}

// ============================================================================
//  Status checks
// ============================================================================

bool ProcessMemory::IsAttached() const {
    if (!m_processHandle || m_processHandle == INVALID_HANDLE_VALUE) return false;

    // Check if the process is still alive
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_processHandle, &exitCode)) return false;

    return (exitCode == STILL_ACTIVE);
}

// ============================================================================
//  Private helpers
// ============================================================================

DWORD ProcessMemory::FindProcessId(const std::wstring& processName) const {
    DWORD pid = FindProcessIdExact(processName);
    if (pid != 0) return pid;

    // Fallback: any Fortnite shipping client executable
    return FindProcessIdFortniteClient();
}

DWORD ProcessMemory::FindProcessIdExact(const std::wstring& processName) const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

DWORD ProcessMemory::FindProcessIdFortniteClient() const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            const wchar_t* exe = entry.szExeFile;
            if (wcsstr(exe, L"Fortnite") != nullptr &&
                wcsstr(exe, L"Shipping") != nullptr) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

std::vector<std::wstring> ProcessMemory::ListProcessesContaining(const std::wstring& substring) {
    std::vector<std::wstring> results;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return results;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (wcsstr(entry.szExeFile, substring.c_str()) != nullptr) {
                results.emplace_back(entry.szExeFile);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return results;
}

void ProcessMemory::SetAttachDiagnostic(const std::wstring& message) const {
    m_lastAttachDiagnostic = message;
}

uintptr_t ProcessMemory::GetImageBaseFromPeb() const {
    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(
        HANDLE, ULONG, PVOID, ULONG, PULONG);

    auto fn = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!fn || !m_processHandle) return 0;

    struct PROCESS_BASIC_INFORMATION {
        NTSTATUS ExitStatus;
        PVOID PebBaseAddress;
        ULONG_PTR AffinityMask;
        LONG BasePriority;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    };

    PROCESS_BASIC_INFORMATION pbi = {};
    ULONG returned = 0;
    constexpr ULONG ProcessBasicInformation = 0;

    if (fn(m_processHandle, ProcessBasicInformation, &pbi, sizeof(pbi), &returned) != 0) {
        return 0;
    }
    if (!pbi.PebBaseAddress) return 0;

    // x64 PEB.ImageBaseAddress
    return Read<uintptr_t>(reinterpret_cast<uintptr_t>(pbi.PebBaseAddress) + 0x10);
}

size_t ProcessMemory::GetImageSizeFromBase(uintptr_t imageBase) const {
    if (imageBase == 0) return 0;

    const uint16_t dosMagic = Read<uint16_t>(imageBase);
    if (dosMagic != 0x5A4D) return 0;  // "MZ"

    const int32_t peOffset = Read<int32_t>(imageBase + 0x3C);
    const uint32_t peSignature = Read<uint32_t>(imageBase + static_cast<uintptr_t>(peOffset));
    if (peSignature != 0x00004550) return 0;  // "PE\0\0"

    const uintptr_t optionalHeader = imageBase + static_cast<uintptr_t>(peOffset) + 4 + 20;
    const uint16_t magic = Read<uint16_t>(optionalHeader);
    if (magic == 0x20B) {  // PE32+
        return Read<uint32_t>(optionalHeader + 56);
    }
    if (magic == 0x10B) {  // PE32
        return Read<uint32_t>(optionalHeader + 56);
    }

    return 0;
}

HWND ProcessMemory::FindGameWindow(const std::wstring& windowTitle) const {
    return FindWindowW(nullptr, windowTitle.c_str());
}

bool ProcessMemory::OpenProcessHandle(DWORD pid) {
    // Close any existing handle
    if (m_processHandle && m_processHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_processHandle);
    }

    const DWORD accessLevels[] = {
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        PROCESS_ALL_ACCESS,
    };

    m_processHandle = nullptr;
    for (DWORD access : accessLevels) {
        m_processHandle = OpenProcess(access, FALSE, pid);
        if (m_processHandle && m_processHandle != INVALID_HANDLE_VALUE) {
            break;
        }
    }

    if (!m_processHandle || m_processHandle == INVALID_HANDLE_VALUE) {
        m_processHandle = nullptr;
        return false;
    }

    m_processId = pid;
    m_baseAddress = 0;
    m_baseSize = 0;

    return true;
}

bool ProcessMemory::ResolveModuleInfo(const std::wstring& moduleName) {
    const std::wstring& targetName = moduleName.empty() ? m_moduleName : moduleName;
    m_lastAttachDiagnostic.clear();

    m_baseAddress = GetModuleBase(targetName);
    m_baseSize = GetModuleSize(targetName);

    if (m_baseAddress == 0) {
        m_baseAddress = GetImageBaseFromPeb();
        if (m_baseAddress != 0) {
            m_lastAttachDiagnostic += L"Resolved image base via PEB.\n";
            m_baseSize = GetImageSizeFromBase(m_baseAddress);
        }
    }

    if (m_baseAddress == 0) {
        const DWORD snapErr = GetLastError();
        m_lastAttachDiagnostic += L"Toolhelp snapshot error: " +
            std::to_wstring(snapErr) + L" (5 = access denied)\n";

        SetLastError(0);
        HMODULE dummy[1] = {};
        DWORD bytesNeeded = 0;
        EnumProcessModules(m_processHandle, dummy, 0, &bytesNeeded);
        m_lastAttachDiagnostic += L"EnumProcessModules error: " +
            std::to_wstring(GetLastError()) + L"\n";

        if (!ProcessMemory::IsRunningElevated()) {
            m_lastAttachDiagnostic +=
                L"Fix: close this window and run Command Prompt as Administrator.";
        } else {
            m_lastAttachDiagnostic +=
                L"Running as admin but still denied — Easy Anti-Cheat is blocking "
                L"external memory tools on this game.";
        }
        return false;
    }

    if (m_baseSize == 0) {
        m_baseSize = GetImageSizeFromBase(m_baseAddress);
    }
    if (m_baseSize == 0) {
        m_baseSize = 0x10000000;  // 256 MB fallback for scanning
    }

    // Verify we can actually read game memory
    uint16_t mz = 0;
    if (!ReadRaw(m_baseAddress, &mz, sizeof(mz)) || mz != 0x5A4D) {
        m_lastAttachDiagnostic =
            L"Found base 0x" + [&]() {
                wchar_t buf[32];
                swprintf_s(buf, L"%llX", static_cast<unsigned long long>(m_baseAddress));
                return std::wstring(buf);
            }() +
            L" but ReadProcessMemory failed (error " + std::to_wstring(GetLastError()) +
            L"). EAC is blocking memory reads.";
        m_baseAddress = 0;
        m_baseSize = 0;
        return false;
    }

    m_lastAttachDiagnostic.clear();
    return true;
}

} // namespace mem
