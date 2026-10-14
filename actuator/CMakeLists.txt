cmake_minimum_required(VERSION 3.25)
project(NeuroPace_Actuator
    VERSION     0.1.0
    LANGUAGES   CXX
    DESCRIPTION "NeuroPace RDNA — Actuator & Scheduler Module"
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

# ── ADLX SDK (Optional) ────────────────────────────────────────
set(ADLX_SDK_DIR "" CACHE PATH "Path to AMD ADLX SDK root directory")
option(NEUROPACE_USE_ADLX "Enable AMD ADLX GPU TGP control (requires ADLX SDK)" OFF)

if(NEUROPACE_USE_ADLX AND ADLX_SDK_DIR)
    if(NOT EXISTS "${ADLX_SDK_DIR}/SDK/Include/ADLX.h")
        message(FATAL_ERROR "ADLX SDK not found at ${ADLX_SDK_DIR}/SDK/Include/ADLX.h")
    endif()
    set(ADLX_FOUND TRUE)
    message(STATUS "ADLX SDK found: ${ADLX_SDK_DIR}")
else()
    set(ADLX_FOUND FALSE)
    message(STATUS "ADLX SDK disabled — TGP control will use mock")
endif()

# ── Source Files ────────────────────────────────────────────────
set(ACTUATOR_SOURCES
    src/main.cpp
    src/ipc_action_subscriber.cpp
    src/process_scheduler.cpp
    src/adlx_actuator.cpp
    src/safety_guard.cpp
)

# ── Executable ──────────────────────────────────────────────────
add_executable(neuropace-actuator ${ACTUATOR_SOURCES})

target_include_directories(neuropace-actuator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(ADLX_FOUND)
    target_include_directories(neuropace-actuator PRIVATE
        ${ADLX_SDK_DIR}/SDK/Include
        ${ADLX_SDK_DIR}/SDK/ADLXHelper/Windows/Cpp
    )
    target_sources(neuropace-actuator PRIVATE
        ${ADLX_SDK_DIR}/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
        ${ADLX_SDK_DIR}/SDK/Platform/Windows/WinAPIs.cpp
    )
    target_compile_definitions(neuropace-actuator PRIVATE NEUROPACE_HAS_ADLX=1)
endif()

target_link_libraries(neuropace-actuator PRIVATE
    nlohmann_json::nlohmann_json
    advapi32        # Security/privilege APIs
    ole32           # COM (ADLX)
)

target_compile_definitions(neuropace-actuator PRIVATE
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(neuropace-actuator PRIVATE
        /W4 /WX-
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
else()
    target_compile_options(neuropace-actuator PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

install(TARGETS neuropace-actuator RUNTIME DESTINATION bin)

# ── Tests ────────────────────────────────────────────────────────
option(NEUROPACE_BUILD_TESTS "Build unit tests" ON)
if(NEUROPACE_BUILD_TESTS)
    enable_testing()

    add_executable(test_safety_guard
        tests/test_safety_guard.cpp
        src/safety_guard.cpp
    )
    target_include_directories(test_safety_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_safety_guard PRIVATE nlohmann_json::nlohmann_json advapi32)
    target_compile_definitions(test_safety_guard PRIVATE
        _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(test_safety_guard PRIVATE /W4 /WX- /permissive- /Zc:__cplusplus /utf-8 $<$<CONFIG:Release>:/UNDEBUG>)
    else()
        target_compile_options(test_safety_guard PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME safety_guard COMMAND test_safety_guard)

    add_executable(test_scheduler
        tests/test_scheduler.cpp
        src/process_scheduler.cpp
        src/safety_guard.cpp
    )
    target_include_directories(test_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_scheduler PRIVATE nlohmann_json::nlohmann_json advapi32)
    target_compile_definitions(test_scheduler PRIVATE
        _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(test_scheduler PRIVATE /W4 /WX- /permissive- /Zc:__cplusplus /utf-8 $<$<CONFIG:Release>:/UNDEBUG>)
    else()
        target_compile_options(test_scheduler PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME scheduler COMMAND test_scheduler)
endif()
