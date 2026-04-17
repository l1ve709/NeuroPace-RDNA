cmake_minimum_required(VERSION 3.25)
project(NeuroPace_Telemetry
    VERSION     0.1.0
    LANGUAGES   C CXX
    DESCRIPTION "NeuroPace RDNA — Telemetry & Sensor Module"
)

# ── C++20 Standard ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── vcpkg Integration ───────────────────────────────────────────
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file"
    )
endif()

# ── Dependencies ────────────────────────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)

# ── ADLX SDK (Optional — build with -DADLX_SDK_DIR=...) ───────
set(ADLX_SDK_DIR "" CACHE PATH "Path to AMD ADLX SDK root directory")
option(NEUROPACE_USE_ADLX "Enable AMD ADLX GPU metrics (requires ADLX SDK)" OFF)

if(NEUROPACE_USE_ADLX AND ADLX_SDK_DIR)
    if(NOT EXISTS "${ADLX_SDK_DIR}/SDK/Include/ADLX.h")
        message(FATAL_ERROR
            "ADLX SDK not found at ${ADLX_SDK_DIR}/SDK/Include/ADLX.h\n"
            "Download from: https://gpuopen.com/adlx/"
        )
    endif()
    message(STATUS "ADLX SDK found: ${ADLX_SDK_DIR}")
    set(ADLX_FOUND TRUE)
else()
    message(STATUS "ADLX SDK disabled — GPU metrics will use mock data")
    set(ADLX_FOUND FALSE)
endif()

# ── Source Files ────────────────────────────────────────────────
set(TELEMETRY_SOURCES
    src/main.cpp
    src/etw_collector.cpp
    src/adlx_sensor.cpp
    src/ipc_publisher.cpp
)

if(NEUROPACE_USE_ADLX AND ADLX_SDK_DIR)
    list(APPEND TELEMETRY_SOURCES "${ADLX_SDK_DIR}/SDK/ADLXHelper/Windows/C/ADLXHelper.c")
    list(APPEND TELEMETRY_SOURCES "${ADLX_SDK_DIR}/SDK/Platform/Windows/WinAPIs.c")
endif()

# ── Executable ──────────────────────────────────────────────────
add_executable(neuropace-telemetry ${TELEMETRY_SOURCES})

target_include_directories(neuropace-telemetry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(ADLX_FOUND)
    target_include_directories(neuropace-telemetry PRIVATE
        ${ADLX_SDK_DIR}/SDK/Include
        ${ADLX_SDK_DIR}/SDK/ADLXHelper/Windows/C
    )
    target_compile_definitions(neuropace-telemetry PRIVATE NEUROPACE_HAS_ADLX=1)
endif()

target_link_libraries(neuropace-telemetry PRIVATE
    nlohmann_json::nlohmann_json
    tdh                     # ETW Trace Data Helper
    advapi32                # ETW StartTrace, ControlTrace, etc.
    ole32                   # COM (ADLX uses COM-like init)
)

target_compile_definitions(neuropace-telemetry PRIVATE
    _WIN32_WINNT=0x0A00     # Target Windows 10+
    NOMINMAX                # Prevent min/max macro pollution
    WIN32_LEAN_AND_MEAN     # Reduce windows.h bloat
    UNICODE
    _UNICODE
)

# ── Compiler Warnings ──────────────────────────────────────────
if(MSVC)
    target_compile_options(neuropace-telemetry PRIVATE
        /W4 /WX-
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
else()
    target_compile_options(neuropace-telemetry PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

# ── Install ─────────────────────────────────────────────────────
install(TARGETS neuropace-telemetry RUNTIME DESTINATION bin)
