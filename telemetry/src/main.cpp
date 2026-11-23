#include "telemetry_data.h"
#include "etw_collector.h"
#include "adlx_sensor.h"
#include "ipc_publisher.h"
#include "telemetry_aggregator.h"   
#include <windows.h>
#include <iostream>
#include <format>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

static std::atomic<bool> g_shutdown{false};
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            std::cout << "\n[MAIN] Shutdown signal received...\n";
            g_shutdown.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}
static bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (::AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup))
    {
        ::CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        ::FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}
static void PrintBanner() {
    std::cout << "NeuroPace RDNA Telemetry & Sensor Module v0.1.0\n";
}
// The parent owns stdin's write end. Closing it ends this read-only capture.
static int CaptureSession(const std::filesystem::path& destination) {
    const HANDLE parent = ::GetStdHandle(STD_INPUT_HANDLE);
    if (::GetFileType(parent) != FILE_TYPE_PIPE) {
        std::cerr << "Session capture requires a parent process with redirected stdin.\n";
        return 2;
    }
    LARGE_INTEGER frequency{};
    if (!::QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 1;
    std::ofstream csv(destination);
    if (!csv) return 1;
    neuropace::EtwCollector etw;
    neuropace::AdlxSensor adlx;
    if (!etw.Start()) std::cerr << "[CAPTURE] ETW unavailable: " << etw.GetLastError() << '\n';
    bool gpuStarted = false;
#ifdef NEUROPACE_HAS_ADLX
    gpuStarted = adlx.Initialize() && adlx.Start();
#endif
    if (!gpuStarted) std::cerr << "[CAPTURE] Hardware GPU telemetry unavailable.\n";
    csv << "timestamp_ms,gpu_clock_mhz,gpu_temp_c,gpu_tgp_w,gpu_utilization_pct,"
           "vram_used_mb,dpc_latency_us,isr_latency_us,dpc_count,isr_count\n";
    while (!g_shutdown.load() && ::PeekNamedPipe(parent, nullptr, 0, nullptr, nullptr, nullptr)) {
        LARGE_INTEGER tick{};
        if (!::QueryPerformanceCounter(&tick)) return 1;
        const double timestamp = static_cast<double>(tick.QuadPart) * 1000.0 / frequency.QuadPart;
        const auto gpu = adlx.GetLatestMetrics();
        const auto kernel = etw.GetLatestMetrics();
        csv << std::format("{:.6f}", timestamp);
        if (gpuStarted && (gpu.vram_total_mb || gpu.gpu_clock_mhz || gpu.gpu_temp_c)) {
            csv << std::format(",{},{},{},{},{}", gpu.gpu_clock_mhz, gpu.gpu_temp_c,
                               gpu.gpu_tgp_w, gpu.gpu_utilization_pct, gpu.vram_used_mb);
        } else {
            csv << ",,,,,";
        }
        if (etw.IsRunning() && kernel.dpc_count) csv << ',' << kernel.dpc_latency_us;
        else csv << ',';
        if (etw.IsRunning() && kernel.isr_count) csv << ',' << kernel.isr_latency_us;
        else csv << ',';
        if (etw.IsRunning()) csv << ',' << kernel.dpc_count << ',' << kernel.isr_count;
        else csv << ",,";
        csv << '\n' << std::flush;
        if (!csv) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    if (argc == 3 && std::wstring_view(argv[1]) == L"--session-csv") {
        return CaptureSession(argv[2]);
    }
    if (argc != 1) {
        std::cerr << "Usage: neuropace-telemetry [--session-csv PATH]\n";
        return 2;
    }
    PrintBanner();
    if (!IsRunningAsAdmin()) {
        std::cerr << "[MAIN] WARNING: Not running as Administrator!\n";
        std::cerr << "[MAIN] ETW kernel tracing requires elevated privileges.\n";
        std::cerr << "[MAIN] Attempting to continue — ETW may fail.\n\n";
    } else {
        std::cout << "[MAIN] Running with Administrator privileges ✓\n";
    }
    neuropace::EtwConfig etwConfig;
    etwConfig.capture_dpc     = true;
    etwConfig.capture_isr     = true;
    etwConfig.flush_timer_ms  = 1;
    neuropace::AdlxConfig adlxConfig;
    adlxConfig.poll_interval_ms = 16;    
    neuropace::PipeConfig telemetryPipeConfig;
    telemetryPipeConfig.pipe_name = L"\\\\.\\pipe\\neuropace-telemetry";
    telemetryPipeConfig.max_instances = 4;
    neuropace::PipeConfig dashboardPipeConfig;
    dashboardPipeConfig.pipe_name = L"\\\\.\\pipe\\neuropace-dashboard";
    dashboardPipeConfig.max_instances = 4;
    neuropace::AggregatorConfig aggConfig;
    aggConfig.telemetry_interval_ms = 5;    
    aggConfig.dashboard_interval_ms = 33;   
    aggConfig.enable_console_log    = true;
    aggConfig.console_log_interval  = 200;  
    std::cout << "\n[MAIN] Initializing components...\n";
    neuropace::EtwCollector  etw(etwConfig);
    neuropace::AdlxSensor    adlx(adlxConfig);
    neuropace::IpcPublisher  telemetryPipe(telemetryPipeConfig);
    neuropace::IpcPublisher  dashboardPipe(dashboardPipeConfig);
    if (!adlx.Initialize()) {
        std::cerr << "[MAIN] ADLX initialization failed: " << adlx.GetLastError() << "\n";
    }
    std::cout << "[MAIN] Starting services...\n\n";
    if (!telemetryPipe.Start()) {
        std::cerr << "[MAIN] Failed to start telemetry pipe: "
                  << telemetryPipe.GetLastError() << "\n";
        return 1;
    }
    if (!dashboardPipe.Start()) {
        std::cerr << "[MAIN] Failed to start dashboard pipe: "
                  << dashboardPipe.GetLastError() << "\n";
        telemetryPipe.Stop();
        return 1;
    }
    if (!etw.Start()) {
        std::cerr << "[MAIN] WARNING: ETW failed to start: " << etw.GetLastError() << "\n";
        std::cerr << "[MAIN] Continuing without DPC/ISR data (mock zeros)\n";
    }
    if (!adlx.Start()) {
        std::cerr << "[MAIN] WARNING: ADLX sensor failed to start: "
                  << adlx.GetLastError() << "\n";
    }
    neuropace::TelemetryAggregator aggregator(
        etw, adlx, telemetryPipe, dashboardPipe, aggConfig
    );
    if (!aggregator.Start()) {
        std::cerr << "[MAIN] Failed to start aggregator\n";
        etw.Stop();
        adlx.Stop();
        telemetryPipe.Stop();
        dashboardPipe.Stop();
        return 1;
    }
    std::cout << "===========================================================\n";
    std::cout << "  Press Ctrl+C to stop the telemetry service\n";
    std::cout << "===========================================================\n\n" << std::flush;
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\n[MAIN] Shutting down...\n";
    aggregator.Stop();
    etw.Stop();
    adlx.Stop();
    telemetryPipe.Stop();
    dashboardPipe.Stop();
    std::cout << "\n[MAIN] NeuroPace RDNA Telemetry Service terminated cleanly.\n";
    std::cout << std::format("[MAIN] Total frames produced: {}\n", aggregator.GetFrameCount());
    return 0;
}
