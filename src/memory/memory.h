// ============================================================================
//  MEMORY READER — Process memory access wrapper
//  Attach to Fortnite, read its memory like an open book.
// ============================================================================

#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <optional>

namespace mem {

class ProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory();

    // No copying — we own a HANDLE
    ProcessMemory(const ProcessMemory&) = delete;
    ProcessMemory& operator=(const ProcessMemory&) = delete;

    /// Attach to a process by its main window title
    bool Attach(const std::wstring& windowTitle);

    /// Attach to a process by its executable name (e.g., L"FortniteClient-Win64-Shipping.exe")
    bool AttachByName(const std::wstring& processName);

    /// Get the base address of a loaded module within the target process
    uintptr_t GetModuleBase(const std::wstring& moduleName) const;

    /// Get the size of a loaded module
    size_t GetModuleSize(const std::wstring& moduleName) const;

    /// Read raw bytes from the target process memory
    bool ReadRaw(uintptr_t address, void* buffer, size_t size) const;

    /// Templated read — reads a single value of type T from memory
    template<typename T>
    T Read(uintptr_t address) const {
        T value{};
        ReadRaw(address, &value, sizeof(T));
        return value;
    }

    /// Read a chain of pointers (multi-level pointer resolution)
    /// Example: ReadPointerChain(base, {0x10, 0x20, 0x30}) reads:
    ///   [[[base + 0x10] + 0x20] + 0x30]
    uintptr_t ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets) const;

    /// Read an ASCII string from memory
    std::string ReadString(uintptr_t address, size_t maxLength = 64) const;

    /// Read a wide (UTF-16) string from memory
    std::wstring ReadWString(uintptr_t address, size_t maxLength = 64) const;

    /// Read an Unreal FString (pointer to wide char data with length prefix)
    std::string ReadFString(uintptr_t address) const;

    /// Check if the process is still running and we're still attached
    bool IsAttached() const;

    /// Get the target process ID
    DWORD GetProcessId() const { return m_processId; }

    /// Get the main module's base address
    uintptr_t GetBaseAddress() const { return m_baseAddress; }

    /// Human-readable reason for the last failed Attach/AttachByName call
    std::wstring GetLastAttachDiagnostic() const { return m_lastAttachDiagnostic; }

    /// List running process names that contain a substring (for troubleshooting)
    static std::vector<std::wstring> ListProcessesContaining(const std::wstring& substring);

    /// Whether this process was launched with admin rights (elevated)
    static bool IsRunningElevated();

private:
    HANDLE    m_processHandle = nullptr;
    DWORD     m_processId = 0;
    uintptr_t m_baseAddress = 0;
    size_t    m_baseSize = 0;
    std::wstring m_moduleName;
    mutable std::wstring m_lastAttachDiagnostic;
    bool      m_useSyscalls = true;  // Prefer syscall layer when available

    /// Find a process ID by executable name using Toolhelp32 snapshot
    DWORD FindProcessId(const std::wstring& processName) const;
    DWORD FindProcessIdExact(const std::wstring& processName) const;
    DWORD FindProcessIdFortniteClient() const;

    uintptr_t GetImageBaseFromPeb() const;
    size_t GetImageSizeFromBase(uintptr_t imageBase) const;

    void SetAttachDiagnostic(const std::wstring& message) const;

    /// Find the game window by title
    HWND FindGameWindow(const std::wstring& windowTitle) const;

    /// Internal: open a handle to the process
    bool OpenProcessHandle(DWORD pid);

    /// Internal: resolve the main module's base address and size
    bool ResolveModuleInfo(const std::wstring& moduleName = L"");
};

} // namespace mem
