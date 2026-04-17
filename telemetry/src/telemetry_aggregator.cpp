#pragma once   
#include "telemetry_data.h"
#include "etw_collector.h"
#include "adlx_sensor.h"
#include "ipc_publisher.h"
#include <iostream>
#include <format>
#include <atomic>
#include <thread>
#include <chrono>
namespace neuropace {
struct AggregatorConfig {
    uint32_t telemetry_interval_ms = 10;    
    uint32_t dashboard_interval_ms = 33;    
    bool     enable_console_log    = false;  
    uint32_t console_log_interval  = 100;    
};
class TelemetryAggregator {
public:
    TelemetryAggregator(
        EtwCollector&  etw,
        AdlxSensor&    adlx,
        IpcPublisher&  telemetryPipe,
        IpcPublisher&  dashboardPipe,
        const AggregatorConfig& config = {}
    )
        : m_etw(etw)
        , m_adlx(adlx)
        , m_telemetryPipe(telemetryPipe)
        , m_dashboardPipe(dashboardPipe)
        , m_config(config)
    {}
    bool Start() {
        if (m_running.load()) return false;
        m_running.store(true);
        m_sequenceId = 0;
        m_telemetryThread = std::thread([this]() {
            using clock = std::chrono::steady_clock;
            while (m_running.load()) {
                auto start = clock::now();
                TelemetryFrame frame = BuildFrame();
                m_telemetryPipe.Publish(frame);
                if (m_config.enable_console_log &&
                    (frame.sequence_id % m_config.console_log_interval == 0))
                {
                    PrintFrame(frame);
                }
                auto elapsed = clock::now() - start;
                auto target  = std::chrono::milliseconds(m_config.telemetry_interval_ms);
                if (elapsed < target) {
                    std::this_thread::sleep_for(target - elapsed);
                }
            }
        });
        m_dashboardThread = std::thread([this]() {
            using clock = std::chrono::steady_clock;
            while (m_running.load()) {
                auto start = clock::now();
                TelemetryFrame frame = BuildFrame();
                m_dashboardPipe.Publish(frame);
                auto elapsed = clock::now() - start;
                auto target  = std::chrono::milliseconds(m_config.dashboard_interval_ms);
                if (elapsed < target) {
                    std::this_thread::sleep_for(target - elapsed);
                }
            }
        });
        std::cout << std::format(
            "[AGG] Aggregator started — telemetry: {}ms, dashboard: {}ms\n",
            m_config.telemetry_interval_ms,
            m_config.dashboard_interval_ms
        );
        return true;
    }
    void Stop() {
        m_running.store(false);
        if (m_telemetryThread.joinable()) m_telemetryThread.join();
        if (m_dashboardThread.joinable()) m_dashboardThread.join();
        std::cout << std::format(
            "[AGG] Stopped — {} total frames produced\n", m_sequenceId.load()
        );
    }
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] uint64_t GetFrameCount() const noexcept { return m_sequenceId.load(); }
private:
    TelemetryFrame BuildFrame() {
        TelemetryFrame frame;
        frame.timestamp_us = GetTimestampMicroseconds();
        frame.sequence_id  = m_sequenceId.fetch_add(1);
        frame.dpc_isr = m_etw.GetLatestMetrics();
        frame.gpu = m_adlx.GetLatestMetrics();
        frame.frame_time_ms = 0.0;
        frame.fps_instant   = 0.0;
        return frame;
    }
    void PrintFrame(const TelemetryFrame& f) {
        std::cout << std::format(
            "[#{:>6}] DPC: {:>7.1f}µs (avg:{:>6.1f}) | ISR: {:>6.1f}µs | "
            "GPU: {:>4}MHz {:>2}°C {:>3}W | VRAM: {:>5}/{:>5}MB | "
            "FPS: {:>6.1f} | Clients: T:{} D:{}\n",
            f.sequence_id,
            f.dpc_isr.dpc_latency_us, f.dpc_isr.dpc_avg_us,
            f.dpc_isr.isr_latency_us,
            f.gpu.gpu_clock_mhz, f.gpu.gpu_temp_c, f.gpu.gpu_tgp_w,
            f.gpu.vram_used_mb, f.gpu.vram_total_mb,
            f.fps_instant,
            m_telemetryPipe.GetClientCount(),
            m_dashboardPipe.GetClientCount()
        );
    }
    EtwCollector&  m_etw;
    AdlxSensor&    m_adlx;
    IpcPublisher&  m_telemetryPipe;
    IpcPublisher&  m_dashboardPipe;
    AggregatorConfig m_config;
    std::thread       m_telemetryThread;
    std::thread       m_dashboardThread;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_sequenceId{0};
};
} 
