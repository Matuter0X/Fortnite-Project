// ============================================================================
//  FORTNITE EXTERNAL ESP — MAIN ENTRY POINT
//  The beating heart of the operation. Everything starts and ends here.
//  
//  Architecture: Init → Attach → Loop (Read → Update → Render) → Cleanup
//  
//  "Code is fiction until it executes." — ENI & LO, 2026
// ============================================================================

#include <Windows.h>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdarg>
#include <vector>

// Our subsystems
#include "config.h"
#include "memory/memory.h"
#include "memory/syscalls.h"
#include "offsets/offsets.h"
#include "offsets/scanner.h"
#include "entity/entity.h"
#include "entity/math.h"
#include "overlay/overlay.h"
#include "overlay/renderer.h"
#include "stealth/stealth.h"
#include "dumper/dumper.h"
#include "dumper/validator.h"
#include "demo/demo.h"

// ImGui (for input handling in main loop)
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// ============================================================================
//  Forward declarations
// ============================================================================

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam
);

// ============================================================================
//  Globals — yeah yeah, globals are evil. Sue me.
// ============================================================================

static bool g_running = true;
static bool g_espActive = true;
static bool g_menuVisible = false;

// ============================================================================
//  Console output — debug builds always; Release shows console for --dump
// ============================================================================

static bool g_consoleEnabled = false;

static void LogPrint(const char* fmt, ...) {
    if (!g_consoleEnabled) return;
    va_list args;
    va_start(args, fmt);
    printf("[ESP] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

#ifdef _DEBUG
    #define LOG(fmt, ...) LogPrint(fmt, ##__VA_ARGS__)
#else
    #define LOG(fmt, ...) LogPrint(fmt, ##__VA_ARGS__)
#endif

static void LogWide(const std::wstring& text) {
    if (!g_consoleEnabled || text.empty()) return;

    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;

    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), len, nullptr, nullptr);

    std::string line;
    for (char ch : utf8) {
        if (ch == '\0') continue;
        if (ch == '\n') {
            if (!line.empty()) LogPrint("%s", line.c_str());
            line.clear();
        } else {
            line += ch;
        }
    }
    if (!line.empty()) LogPrint("%s", line.c_str());
}

static bool HasDumpFlag(LPSTR lpCmdLine) {
    if (lpCmdLine && (strstr(lpCmdLine, "--dump") != nullptr || strstr(lpCmdLine, "-d") != nullptr)) {
        return true;
    }
    const char* cmdLine = GetCommandLineA();
    return cmdLine && (strstr(cmdLine, "--dump") != nullptr || strstr(cmdLine, "-d") != nullptr);
}

static bool HasDemoFlag(LPSTR lpCmdLine) {
    if (lpCmdLine && (strstr(lpCmdLine, "--demo") != nullptr)) {
        return true;
    }
    const char* cmdLine = GetCommandLineA();
    return cmdLine && (strstr(cmdLine, "--demo") != nullptr);
}

static bool IsAntiCheatBlocked(const std::wstring& diagnostic) {
    return diagnostic.find(L"Easy Anti-Cheat") != std::wstring::npos ||
           diagnostic.find(L"access denied") != std::wstring::npos ||
           diagnostic.find(L"error: 5") != std::wstring::npos;
}

void SetupConsole(bool force = false) {
#ifdef _DEBUG
    force = true;
#endif
    if (!force || g_consoleEnabled) return;

    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    SetConsoleTitleW(L"FortniteESP");
    g_consoleEnabled = true;
    LOG("Console initialized");
}

void CleanupConsole() {
    if (!g_consoleEnabled) return;
    FreeConsole();
    g_consoleEnabled = false;
}

// ============================================================================
//  Hotkey processing — runs every frame
// ============================================================================

void ProcessHotkeys() {
    // INSERT — toggle menu visibility
    static bool insertWasDown = false;
    bool insertIsDown = (GetAsyncKeyState(config::g_settings.toggleKey) & 0x8000) != 0;
    if (insertIsDown && !insertWasDown) {
        g_menuVisible = !g_menuVisible;
        LOG("Menu %s", g_menuVisible ? "OPENED" : "CLOSED");
    }
    insertWasDown = insertIsDown;

    // HOME — toggle ESP on/off
    static bool homeWasDown = false;
    bool homeIsDown = (GetAsyncKeyState(config::g_settings.espToggleKey) & 0x8000) != 0;
    if (homeIsDown && !homeWasDown) {
        g_espActive = !g_espActive;
        LOG("ESP %s", g_espActive ? "ENABLED" : "DISABLED");
    }
    homeWasDown = homeIsDown;

    // END — exit application
    if (GetAsyncKeyState(config::g_settings.exitKey) & 0x8000) {
        LOG("Exit key pressed — shutting down");
        g_running = false;
    }
}

// ============================================================================
//  MAIN — where the magic happens
// ============================================================================

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    const bool dumpOnlyMode = HasDumpFlag(lpCmdLine);
    bool demoMode = HasDemoFlag(lpCmdLine);
    SetupConsole(dumpOnlyMode || demoMode);

    LOG("=== FORTNITE EXTERNAL ESP ===");
    LOG("Elevated (Administrator): %s", mem::ProcessMemory::IsRunningElevated() ? "YES" : "NO");
    if (!mem::ProcessMemory::IsRunningElevated()) {
        LOG("WARNING: Not running as Administrator — attach will fail with error 5.");
        if (dumpOnlyMode) {
            MessageBoxW(nullptr,
                L"This tool must run as Administrator to read Fortnite's memory.\n\n"
                L"Close this window, then right-click FortniteESP.exe and choose "
                L"\"Run as administrator\".",
                L"FortniteESP — Administrator Required",
                MB_OK | MB_ICONWARNING);
        }
    }

    // ========================================================================
    //  PHASE 1: Stealth initialization — do this FIRST
    // ========================================================================
    LOG("Initializing stealth measures...");

    if (!stealth::Initialize()) {
        LOG("WARNING: Some stealth measures failed to initialize");
        // Continue anyway — not all checks are fatal
    }
    LOG("Stealth layer active");

    // ========================================================================
    //  PHASE 2: Load configuration
    // ========================================================================

    LOG("Loading configuration...");
    if (!config::Load("config.json")) {
        LOG("No config file found — using defaults");
        config::Save("config.json");  // Create default config
    }
    LOG("Config loaded");

    // ========================================================================
    //  PHASE 3: Initialize syscall layer
    // ========================================================================

    LOG("Initializing direct syscalls...");
    if (!syscall::Initialize()) {
        LOG("WARNING: Syscall initialization failed — falling back to standard API");
        // Will use ReadProcessMemory instead
    } else {
        LOG("Syscall layer active — ntdll hooks bypassed");
    }

    // ========================================================================
    //  PHASE 4: Attach to Fortnite process (skipped in demo mode)
    // ========================================================================

    mem::ProcessMemory memory;

    std::wstring targetProcess(
        config::g_settings.targetProcess.begin(),
        config::g_settings.targetProcess.end()
    );

    std::wstring targetWindowTitle(
        config::g_settings.targetWindow.begin(),
        config::g_settings.targetWindow.end()
    );

    bool attached = demoMode;

    if (demoMode) {
        LOG("=== DEMO MODE ===");
        LOG("Previewing ESP with synthetic players (no game attach).");
    } else {
        LOG("Looking for Fortnite (%ls)...", targetProcess.c_str());
        if (dumpOnlyMode) {
            LOG("Dump mode: waiting for Fortnite to start (Ctrl+C to cancel)...");
        }

        int retryCount = 0;
        while (!attached) {
            if (memory.AttachByName(targetProcess)) {
                attached = true;
                break;
            }

            if (!targetWindowTitle.empty() && memory.Attach(targetWindowTitle)) {
                LOG("Attached using game window title instead of process name.");
                attached = true;
                break;
            }

            const std::wstring& diag = memory.GetLastAttachDiagnostic();

            if (mem::ProcessMemory::IsRunningElevated() && IsAntiCheatBlocked(diag)) {
                if (dumpOnlyMode) {
                    LOG("Anti-cheat blocked memory access — cannot dump live Fortnite.");
                    MessageBoxW(nullptr,
                        L"Easy Anti-Cheat is blocking memory access.\n\n"
                        L"Live offset dumping is not possible with this tool.\n"
                        L"Run without --dump to preview ESP in demo mode instead:\n"
                        L"  FortniteESP.exe",
                        L"FortniteESP — Blocked by Anti-Cheat",
                        MB_OK | MB_ICONERROR);
                    stealth::Cleanup();
                    CleanupConsole();
                    return 1;
                }

                LOG("Anti-cheat blocked memory access — switching to DEMO mode.");
                demoMode = true;
                break;
            }

            if (!g_running) {
                stealth::Cleanup();
                CleanupConsole();
                return 0;
            }

            if (retryCount % 10 == 0) {
                LOG("Waiting for Fortnite to start... (attempt %d)", retryCount);
                if (!diag.empty()) {
                    LOG("Attach failed:");
                    LogWide(diag);
                }
                if (retryCount >= 10 && !mem::ProcessMemory::IsRunningElevated()) {
                    LOG("Tip: run as Administrator if attach keeps failing.");
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            retryCount++;
        }

        if (attached) {
            LOG("Attached to Fortnite (PID: %lu, Base: 0x%llX)",
                memory.GetProcessId(), memory.GetBaseAddress());
        }
    }

    // ========================================================================
    //  PHASE 5: Load offsets (skipped in demo mode)
    // ========================================================================

    if (!demoMode) {
        LOG("Loading offsets...");
        scanner::Initialize(memory);

        dumper::SDKDumper sdkDumper;
        bool dumpedSuccessfully = false;

        LOG("Initializing SDK dumper (scanning for GNames & GObjects)...");
        if (sdkDumper.Initialize(memory)) {
        LOG("SDK Dumper ready! GNames=0x%llX, GObjects=0x%llX",
            sdkDumper.GetGNamesAddress(), sdkDumper.GetGObjectsAddress());

        // Auto-dump offsets
        LOG("Dumping offsets from game memory...");
        dumpedSuccessfully = sdkDumper.DumpAndUpdateOffsets("offsets.json",
            [](const std::string& status, int pct) {
                (void)status;
                (void)pct;
                LOG("  [%d%%] %s", pct, status.c_str());
            }
        );

        if (dumpedSuccessfully) {
            LOG("=== OFFSET DUMP SUCCESSFUL ===");
            LOG("offsets.json has been updated with fresh values!");
        } else {
            LOG("WARNING: Dumper initialized but offset extraction failed");
        }

        // If --dump flag: also dump the full SDK to a text file and exit
        if (dumpOnlyMode) {
            LOG("Dump-only mode — generating full SDK dump...");
            sdkDumper.DumpFullSDK("sdk_dump.txt",
                [](const std::string& status, int pct) {
                    (void)status;
                    (void)pct;
                    LOG("  [%d%%] %s", pct, status.c_str());
                }
            );
            LOG("Full SDK written to sdk_dump.txt");
            LOG("Exiting (dump-only mode).");

            if (dumpedSuccessfully) {
                MessageBoxW(nullptr,
                    L"Dump complete.\n\noffsets.json and sdk_dump.txt were written to this folder.",
                    L"FortniteESP — Dump OK", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(nullptr,
                    L"Connected to Fortnite but offset extraction failed.\n\n"
                    L"Check the console for details. The game build may have changed.",
                    L"FortniteESP — Dump Failed", MB_OK | MB_ICONWARNING);
            }

            stealth::Cleanup();
            CleanupConsole();
            return dumpedSuccessfully ? 0 : 1;
        }
    } else {
        LOG("SDK Dumper failed to initialize (couldn't find GNames/GObjects)");

        if (dumpOnlyMode) {
            MessageBoxW(nullptr,
                L"Could not find GNames/GObjects in Fortnite's memory.\n\n"
                L"Make sure Fortnite is fully loaded (past the lobby), then try again.\n"
                L"Run as Administrator if attach keeps failing.",
                L"FortniteESP — Dump Failed", MB_OK | MB_ICONERROR);
            stealth::Cleanup();
            CleanupConsole();
            return 1;
        }

        LOG("Falling back to file-based offsets...");
    }

    // If dumper didn't work, try loading from file
    if (!dumpedSuccessfully) {
        if (offsets::LoadFromFile("offsets.json")) {
            LOG("Offsets loaded from file");
        } else {
            LOG("No offset file found — attempting signature scan for GWorld...");
            if (scanner::ResolveEngineOffsets()) {
                LOG("GWorld resolved via signature scan");
                offsets::SaveToFile("offsets.json");
            } else {
                LOG("WARNING: Could not resolve offsets. ESP will not work correctly.");
                LOG("Run with --dump flag while Fortnite is open to generate offsets.");
            }
        }
    }

    // ========================================================================
    //  PHASE 5.5: Validate offsets
    // ========================================================================
    
    LOG("Validating offsets against live game memory...");
    auto validationResults = validator::ValidateOffsets(memory);
    validator::PrintResults(validationResults);
    
    if (!validator::IsValid(validationResults)) {
        LOG("CRITICAL WARNING: Offset validation failed! ESP will likely crash or show nothing.");
        LOG("Please run with --dump to extract fresh offsets.");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    } // !demoMode

    // ========================================================================
    //  PHASE 6: Create overlay window
    // ========================================================================

    LOG("Creating overlay window...");
    
    overlay::OverlayWindow overlayWindow;

    if (demoMode) {
        if (!overlayWindow.CreateStandalone()) {
            LOG("FATAL: Failed to create demo overlay window!");
            MessageBoxW(nullptr, L"Could not create overlay window", L"Error", MB_OK | MB_ICONERROR);
            stealth::Cleanup();
            CleanupConsole();
            return 1;
        }
    } else {
        while (!overlayWindow.Create(targetWindowTitle)) {
            if (!g_running) {
                config::Save("config.json");
                stealth::Cleanup();
                CleanupConsole();
                return 0;
            }
            LOG("Waiting for Fortnite window...");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    LOG("Overlay window created (%dx%d)", 
        overlayWindow.GetWidth(), overlayWindow.GetHeight());

    // Hide overlay from screen capture
    stealth::HideFromCapture(overlayWindow.GetHandle());
    LOG("Overlay hidden from screen capture");

    // ========================================================================
    //  PHASE 7: Initialize renderer
    // ========================================================================

    LOG("Initializing DirectX 11 renderer...");
    
    renderer::Renderer espRenderer;
    if (!espRenderer.Initialize(
            overlayWindow.GetHandle(), 
            overlayWindow.GetWidth(), 
            overlayWindow.GetHeight())) {
        LOG("FATAL: Failed to initialize renderer!");
        MessageBoxW(nullptr, L"DirectX 11 initialization failed", L"Error", MB_OK | MB_ICONERROR);
        config::Save("config.json");
        stealth::Cleanup();
        CleanupConsole();
        return 0;
    }
    LOG("Renderer initialized");

    // ========================================================================
    //  PHASE 8: Initialize entity system
    // ========================================================================

    LOG("Initializing entity system...");
    entity::EntityList entityList;
    if (!demoMode) {
        entityList.Initialize(&memory);
    }
    LOG("Entity system ready");

    if (demoMode) {
        LOG("=== DEMO MODE ACTIVE ===");
        LOG("Synthetic ESP preview — INSERT menu, HOME toggle ESP, END exit");
    } else {
        LOG("=== ENTERING MAIN LOOP ===");
        LOG("Controls:");
        LOG("  INSERT  — Toggle settings menu");
        LOG("  HOME    — Toggle ESP on/off");
        LOG("  END     — Exit");
    }

    const auto demoStart = std::chrono::steady_clock::now();

    while (g_running) {
        // Process Windows messages
        if (!overlayWindow.ProcessMessages()) {
            LOG("Overlay window closed — exiting");
            break;
        }

        if (!demoMode) {
            overlayWindow.SyncToTarget();

            if (!memory.IsAttached()) {
                LOG("Fortnite process lost — exiting");
                break;
            }
        }

        // Process hotkeys
        ProcessHotkeys();

        // Toggle click-through based on menu state
        overlayWindow.SetClickThrough(!g_menuVisible);

        // Periodically randomize window title (every ~30 seconds)
        static auto lastRandomize = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRandomize).count() >= 30) {
            stealth::RandomizeWindowTitle(overlayWindow.GetHandle());
            lastRandomize = now;
        }

        std::vector<entity::PlayerEntity> demoPlayers;
        if (g_espActive) {
            if (demoMode) {
                const float elapsed = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - demoStart).count();
                demoPlayers = demo::GenerateDemoPlayers(
                    overlayWindow.GetWidth(),
                    overlayWindow.GetHeight(),
                    elapsed
                );
            } else {
                entityList.Update(
                    overlayWindow.GetWidth(),
                    overlayWindow.GetHeight()
                );
            }
        }

        espRenderer.BeginFrame();

        if (g_espActive) {
            if (demoMode) {
                espRenderer.RenderESP(
                    demoPlayers,
                    overlayWindow.GetWidth(),
                    overlayWindow.GetHeight()
                );
            } else {
                espRenderer.RenderESP(
                    entityList.GetPlayers(),
                    overlayWindow.GetWidth(),
                    overlayWindow.GetHeight()
                );
            }
        }

        // Step 4: Draw settings menu if visible
        if (g_menuVisible) {
            espRenderer.RenderMenu(espRenderer.GetConfig());
        }

        // Step 5: Present frame
        espRenderer.EndFrame();

        // Periodic anti-debug check (every ~60 seconds)
        static auto lastDebugCheck = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastDebugCheck).count() >= 60) {
            if (stealth::CheckForDebugger()) {
                LOG("DEBUGGER DETECTED — emergency shutdown!");
                g_running = false;
            }
            lastDebugCheck = now;
        }
    }

    // ========================================================================
    //  PHASE 10: Cleanup — leave no trace
    // ========================================================================

    LOG("=== SHUTTING DOWN ===");

    // Save current config
    config::Save("config.json");
    LOG("Config saved");

    // Shutdown renderer
    espRenderer.Shutdown();
    LOG("Renderer shut down");

    // Cleanup stealth
    stealth::Cleanup();
    LOG("Stealth cleanup complete");

    // Cleanup console
    CleanupConsole();

    return 0;
}
