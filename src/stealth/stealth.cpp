// ============================================================================
//  STEALTH SYSTEM — Implementation
//  
//  Multiple layers of anti-detection:
//  1. Anti-debugger checks (multiple methods)
//  2. Screen capture hiding (SetWindowDisplayAffinity)
//  3. String obfuscation (compile-time XOR)
//  4. Window title randomization
//  5. Process cleanup on exit
// ============================================================================

#include "stealth.h"
#include <random>
#include <chrono>

// For NtQueryInformationProcess
typedef NTSTATUS(WINAPI* pNtQueryInformationProcess)(
    HANDLE, UINT, PVOID, ULONG, PULONG
);

namespace stealth {

// ============================================================================
//  Initialize stealth measures
// ============================================================================

bool Initialize() {
    bool allGood = true;

    // Check for debuggers immediately
    if (CheckForDebugger()) {
        // In production, you'd exit here. For development, just flag it.
        #ifndef _DEBUG
            ExitProcess(0);
        #endif
        allGood = false;
    }

    // Set process priority to normal (don't stand out in task manager)
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    return allGood;
}

// ============================================================================
//  Hide from screen capture
// ============================================================================

bool HideFromCapture(HWND overlayWindow) {
    if (!overlayWindow) return false;

    // WDA_EXCLUDEFROMCAPTURE (0x11) — available on Windows 10 2004+
    // Makes the window completely invisible to ALL screen capture methods:
    // BitBlt, PrintWindow, Desktop Duplication API, OBS, Discord, etc.
    
    // Try the newer flag first
    if (SetWindowDisplayAffinity(overlayWindow, 0x00000011)) {  // WDA_EXCLUDEFROMCAPTURE
        return true;
    }

    // Fallback: WDA_MONITOR (0x01) — shows black rectangle instead of content
    // Available on older Windows 10 versions
    if (SetWindowDisplayAffinity(overlayWindow, 0x00000001)) {  // WDA_MONITOR
        return true;
    }

    return false;
}

// ============================================================================
//  Debugger detection — multiple methods for resilience
// ============================================================================

bool CheckForDebugger() {
    // ================================================================
    // Method 1: IsDebuggerPresent()
    // Basic Win32 check — reads PEB.BeingDebugged flag
    // Easy to bypass but catches script kiddies
    // ================================================================
    if (IsDebuggerPresent()) {
        return true;
    }

    // ================================================================
    // Method 2: CheckRemoteDebuggerPresent()
    // Catches external debuggers (WinDbg, x64dbg attached remotely)
    // ================================================================
    BOOL remoteDebugger = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDebugger)) {
        if (remoteDebugger) return true;
    }

    // ================================================================
    // Method 3: NtQueryInformationProcess — ProcessDebugPort
    // Queries the kernel for debug port info. Returns -1 if debugged.
    // More reliable than PEB checks since it goes through the kernel.
    // ================================================================
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto NtQueryInfo = reinterpret_cast<pNtQueryInformationProcess>(
            GetProcAddress(ntdll, "NtQueryInformationProcess")
        );

        if (NtQueryInfo) {
            // ProcessDebugPort = 7
            DWORD_PTR debugPort = 0;
            NTSTATUS status = NtQueryInfo(
                GetCurrentProcess(),
                7,  // ProcessDebugPort
                &debugPort,
                sizeof(debugPort),
                nullptr
            );

            if (status == 0 && debugPort != 0) {
                return true;
            }

            // ProcessDebugObjectHandle = 30
            HANDLE debugObject = nullptr;
            status = NtQueryInfo(
                GetCurrentProcess(),
                30,  // ProcessDebugObjectHandle
                &debugObject,
                sizeof(debugObject),
                nullptr
            );

            // If we successfully get a debug object handle, we're being debugged
            if (status == 0) {
                return true;
            }
        }
    }

    // ================================================================
    // Method 4: Timing-based detection
    // Debuggers slow down execution. If a simple operation takes
    // way too long, someone is stepping through our code.
    // ================================================================
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // Do something trivial
    volatile int dummy = 0;
    for (int i = 0; i < 100; ++i) {
        dummy += i;
    }

    QueryPerformanceCounter(&end);

    // If this took more than 100ms, something is very wrong
    double elapsed = static_cast<double>(end.QuadPart - start.QuadPart) / freq.QuadPart;
    if (elapsed > 0.1) {
        return true;
    }

    // ================================================================
    // Method 5: Hardware breakpoint detection
    // Check the debug registers via GetThreadContext
    // ================================================================
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (GetThreadContext(GetCurrentThread(), &ctx)) {
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0) {
            return true;
        }
    }

    return false;
}

// ============================================================================
//  Window title randomization
// ============================================================================

void RandomizeWindowTitle(HWND window) {
    if (!window) return;

    // Generate a random string that looks like a legit Windows process
    static const wchar_t* fakeNames[] = {
        L"Microsoft Windows Search Protocol Host",
        L"Windows Audio Device Graph Isolation",
        L"Service Host: Local System",
        L"Windows Presentation Foundation Terminal Server Print",
        L"Application Frame Host",
        L"Microsoft Compatibility Telemetry",
        L"Windows Error Reporting",
        L"System Interrupts",
        L"Cryptographic Services",
        L"Windows Event Log",
        L"Background Intelligent Transfer Service",
        L"Windows Management Instrumentation",
    };

    auto seed = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, 11);

    SetWindowTextW(window, fakeNames[dist(rng)]);
}

// ============================================================================
//  Cleanup — zero sensitive memory, remove traces
// ============================================================================

void Cleanup() {
    // Not much to clean up at the application level.
    // The OS will reclaim everything when we exit.
    // But we can zero out any global buffers we used.
    
    // In a production cheat, you'd also:
    // - Delete the binary after execution (self-delete)
    // - Clear any temp files
    // - Unload from memory cleanly
    // - Remove registry traces
}

} // namespace stealth
