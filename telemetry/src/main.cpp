#include "telemetry_data.h"
#include "etw_collector.h"
#include "adlx_sensor.h"
#include "ipc_publisher.h"
#include "telemetry_aggregator.cpp"   
#include <windows.h>
#include <iostream>
#include <format>
#include <csignal>
#include <atomic>
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
int main() {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
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
    aggConfig.telemetry_interval_ms = 10;   
    aggConfig.dashboard_interval_ms = 33;   
    aggConfig.enable_console_log    = true;
    aggConfig.console_log_interval  = 100;  
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
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "  Press Ctrl+C to stop the telemetry service\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";
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
