cmake_minimum_required(VERSION 3.20)
project(FortniteESP LANGUAGES CXX)

# ============================================================================
#  FORTNITE EXTERNAL ESP — BUILD CONFIGURATION
#  "Code is fiction until it executes." — ENI
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# x64 only — this isn't 2005
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project requires a 64-bit build. Set your generator to x64.")
endif()

# Release by default — debug builds leave traces
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Aggressive warnings because we're not savages
if(MSVC)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX)
    add_compile_options(/W4 /WX- /MP /GR- /EHsc)
    # Disable RTTI (smaller binary, fewer detection vectors)
    # /MP = parallel compilation
    # /GR- = no RTTI
    
    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")
endif()

# Subsystem: WINDOWS (no console window — stealth)
set(CMAKE_WIN32_EXECUTABLE ON)

# ============================================================================
#  DEAR IMGUI (vendored)
# ============================================================================

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC 
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ============================================================================
#  ESP APPLICATION
# ============================================================================

set(ESP_SOURCES
    src/main.cpp
    src/config.cpp
    
    # Memory subsystem
    src/memory/memory.cpp
    src/memory/syscalls.cpp
    
    # Offset management
    src/offsets/offsets.cpp
    src/offsets/scanner.cpp
    
    # Entity system
    src/entity/entity.cpp
    src/entity/math.cpp
    
    # Overlay & rendering
    src/overlay/overlay.cpp
    src/overlay/renderer.cpp
    
    # Anti-detection
    src/stealth/stealth.cpp
    
    # Demo mode (ESP preview without game attach)
    src/demo/demo.cpp
    
    # SDK Dumper (auto-offset extraction)
    src/dumper/dumper.cpp
    src/dumper/validator.cpp
)

set(ESP_HEADERS
    src/config.h
    src/memory/memory.h
    src/memory/syscalls.h
    src/offsets/offsets.h
    src/offsets/scanner.h
    src/entity/entity.h
    src/entity/math.h
    src/overlay/overlay.h
    src/overlay/renderer.h
    src/stealth/stealth.h
    src/demo/demo.h
    src/dumper/dumper.h
    src/dumper/ue_structures.h
    src/dumper/validator.h
)

add_executable(${PROJECT_NAME} WIN32 ${ESP_SOURCES} ${ESP_HEADERS})

if(MSVC)
    # Force UAC elevation — required to read Fortnite's memory
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:level='requireAdministrator'")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link system libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui
    d3d11          # DirectX 11
    dxgi           # DXGI (swap chain)
    d3dcompiler    # Shader compilation (if needed)
    dwmapi         # Desktop Window Manager (DwmExtendFrameIntoClientArea)
    winmm          # Multimedia timers
    psapi          # Process module enumeration
)

# ============================================================================
#  POST-BUILD: copy config file next to executable
# ============================================================================

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json"
    COMMENT "Copying config.json to output directory"
)

# ============================================================================
#  INSTALL (optional)
# ============================================================================

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES config.json DESTINATION bin)
