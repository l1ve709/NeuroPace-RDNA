#pragma once
#include "telemetry_data.h"
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <string>
#include <chrono>

namespace neuropace {

struct AdlxConfig {
    uint32_t poll_interval_ms = 16;    
    bool     enable_hotspot   = true;  
    bool     enable_fan       = true;  
};

class AdlxSensor {
public:
    explicit AdlxSensor(const AdlxConfig& config = {});
    ~AdlxSensor();

    AdlxSensor(const AdlxSensor&) = delete;
    AdlxSensor& operator=(const AdlxSensor&) = delete;

    bool Initialize();
    bool Start();
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] bool IsAdlxAvailable() const noexcept { return m_adlxAvailable; }
    [[nodiscard]] GpuMetrics GetLatestMetrics() const;
    [[nodiscard]] std::string GetGpuName() const;
    [[nodiscard]] std::string GetLastError() const;

private:
    void PollLoop();
    GpuMetrics ReadAdlxMetrics();
    GpuMetrics GenerateMockMetrics();
    void SetError(const std::string& msg);
    void Shutdown();

    AdlxConfig m_config;
    std::thread       m_pollThread;
    std::atomic<bool> m_running{false};
    mutable std::shared_mutex m_metricsMutex;
    GpuMetrics                m_latestMetrics;
    
    std::string m_gpuName = "Unknown AMD GPU";
    bool        m_adlxAvailable = false;
    
    mutable std::shared_mutex m_errorMutex;
    std::string               m_lastError;

    // Use void* for ADLX pointers to avoid header pollution
    void* m_perfService = nullptr;
    void* m_targetGpu   = nullptr;
};

} // namespace neuropace
