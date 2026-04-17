#include "adlx_sensor.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <random>
#include <mutex>
#include <shared_mutex>
#ifdef NEUROPACE_HAS_ADLX
#include "ADLX.h"
#include "ISystem.h"
#include "IPerformanceMonitoring.h"
extern "C" {
#include "ADLXHelper.h"
}
using namespace adlx;
#endif
namespace neuropace {
AdlxSensor::AdlxSensor(const AdlxConfig& config)
    : m_config(config), m_running(false), m_adlxAvailable(false) {
}
AdlxSensor::~AdlxSensor() {
    Stop();
    Shutdown();
}
bool AdlxSensor::Initialize() {
#ifdef NEUROPACE_HAS_ADLX
    ADLX_RESULT res = ADLXHelper_Initialize();
    if (ADLX_FAILED(res)) {
        SetError("ADLX Helper initialization failed");
        return false;
    }
    IADLXSystem* sys = ADLXHelper_GetSystemServices();
    if (!sys) {
        SetError("Failed to get ADLX System Services");
        return false;
    }
    IADLXGPUListPtr gpus;
    res = sys->GetGPUs(&gpus);
    if (ADLX_FAILED(res) || gpus->Size() == 0) {
        SetError("No AMD GPUs detected via ADLX");
        return false;
    }
    IADLXGPU* gpu = nullptr;
    gpus->At(0, &gpu);
    m_targetGpu = static_cast<void*>(gpu);
    const char* gpuNameStr = nullptr;
    if (ADLX_SUCCEEDED(gpu->Name(&gpuNameStr))) {
        m_gpuName = gpuNameStr;
    }
    IADLXPerformanceMonitoringServices* perf = nullptr;
    res = sys->GetPerformanceMonitoringServices(&perf);
    if (ADLX_FAILED(res)) {
        SetError("Performance monitoring services not available");
        return false;
    }
    m_perfService = static_cast<void*>(perf);
    m_adlxAvailable = true;
    std::cout << "[ADLX] " << m_gpuName << " initialized for telemetry." << std::endl;
    return true;
#else
    m_gpuName = "Mock-RDNA3-GPU";
    m_adlxAvailable = true;
    std::cout << "[ADLX] Mock sensor initialized." << std::endl;
    return true;
#endif
}
void AdlxSensor::Shutdown() {
#ifdef NEUROPACE_HAS_ADLX
    if (m_perfService) {
        static_cast<IADLXPerformanceMonitoringServices*>(m_perfService)->Release();
        m_perfService = nullptr;
    }
    if (m_targetGpu) {
        static_cast<IADLXGPU*>(m_targetGpu)->Release();
        m_targetGpu = nullptr;
    }
    ADLXHelper_Terminate();
#endif
    m_adlxAvailable = false;
}
bool AdlxSensor::Start() {
    if (m_running.exchange(true)) return true;
#ifdef NEUROPACE_HAS_ADLX
    if (m_adlxAvailable) {
        auto* perf = static_cast<IADLXPerformanceMonitoringServices*>(m_perfService);
        if (perf) {
            ADLX_RESULT res = perf->StartPerformanceMetricsTracking();
            std::cout << "[ADLX] Start tracking result: " << static_cast<int>(res) << std::endl;
        }
    }
#endif
    m_pollThread = std::thread(&AdlxSensor::PollLoop, this);
    return true;
}
void AdlxSensor::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
}
void AdlxSensor::PollLoop() {
    while (m_running.load()) {
        GpuMetrics metrics = ReadAdlxMetrics();
        {
            std::unique_lock lock(m_metricsMutex);
            m_latestMetrics = metrics;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.poll_interval_ms));
    }
}
GpuMetrics AdlxSensor::ReadAdlxMetrics() {
#ifdef NEUROPACE_HAS_ADLX
    if (!m_adlxAvailable) return GenerateMockMetrics();
    GpuMetrics metrics;
    auto* perf = static_cast<IADLXPerformanceMonitoringServices*>(m_perfService);
    auto* gpu = static_cast<IADLXGPU*>(m_targetGpu);
    if (!perf || !gpu) return metrics;
    IADLXGPUMetricsPtr adlxMetrics;
    if (ADLX_FAILED(perf->GetCurrentGPUMetrics(gpu, &adlxMetrics))) {
        return metrics;
    }
    adlx_int clock = 0;
    adlx_int memClock = 0;
    adlx_double temp = 0;
    adlx_double hotspot = 0;
    adlx_double power = 0;
    adlx_double util = 0;
    adlx_int fan = 0;
    adlx_int vramMB = 0;
    adlxMetrics->GPUClockSpeed(&clock);
    adlxMetrics->GPUVRAMClockSpeed(&memClock);
    adlxMetrics->GPUTemperature(&temp);
    adlxMetrics->GPUHotspotTemperature(&hotspot);
    adlxMetrics->GPUTotalBoardPower(&power);
    adlxMetrics->GPUUsage(&util);
    adlxMetrics->GPUFanSpeed(&fan);
    adlxMetrics->GPUVRAM(&vramMB);
    metrics.gpu_clock_mhz = static_cast<uint32_t>(clock);
    metrics.mem_clock_mhz = static_cast<uint32_t>(memClock);
    metrics.gpu_temp_c = static_cast<uint32_t>(temp);
    metrics.hotspot_temp_c = static_cast<uint32_t>(hotspot);
    metrics.gpu_tgp_w = static_cast<uint32_t>(power);
    metrics.gpu_utilization_pct = util;
    metrics.fan_speed_rpm = static_cast<uint32_t>(fan);
    metrics.vram_used_mb = static_cast<uint64_t>(vramMB);
    IADLXFPSPtr adlxFps;
    ADLX_RESULT res = perf->GetCurrentFPS(&adlxFps);
    if (ADLX_SUCCEEDED(res)) {
        adlx_int fpsVal = 0;
        if (ADLX_SUCCEEDED(adlxFps->FPS(&fpsVal))) {
            metrics.fps = static_cast<uint32_t>(fpsVal);
        }
    } else {
        static uint32_t failCounter = 0;
        if (failCounter++ % 30 == 0) {
             std::cerr << "[ADLX] GetCurrentFPS failed: " << static_cast<int>(res) << std::endl;
        }
    }
    adlx_uint vramTotal = 0;
    if (ADLX_SUCCEEDED(gpu->TotalVRAM(&vramTotal))) {
        metrics.vram_total_mb = static_cast<uint64_t>(vramTotal);
    }
    return metrics;
#else
    return GenerateMockMetrics();
#endif
}
GpuMetrics AdlxSensor::GenerateMockMetrics() {
    thread_local std::mt19937 rng(42);
    thread_local uint64_t tick = 0;
    ++tick;
    const double time_s = static_cast<double>(tick) * m_config.poll_interval_ms / 1000.0;
    const double load_cycle = 0.9 + 0.1 * std::sin(time_s * 0.2);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    double jitter = dist(rng);
    GpuMetrics m;
    m.gpu_clock_mhz = static_cast<uint32_t>(2500 * load_cycle + jitter * 10);
    m.mem_clock_mhz = 2250;
    m.gpu_temp_c = static_cast<uint32_t>(60 * load_cycle + jitter);
    m.hotspot_temp_c = m.gpu_temp_c + 10;
    m.gpu_tgp_w = static_cast<uint32_t>(280 * load_cycle);
    m.gpu_utilization_pct = 95.0 * load_cycle;
    m.vram_used_mb = 12000;
    m.vram_total_mb = 20480;
    m.fan_speed_rpm = static_cast<uint32_t>(1500 * load_cycle);
    return m;
}
GpuMetrics AdlxSensor::GetLatestMetrics() const {
    std::shared_lock lock(m_metricsMutex);
    return m_latestMetrics;
}
std::string AdlxSensor::GetGpuName() const {
    return m_gpuName;
}
void AdlxSensor::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
}
std::string AdlxSensor::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
} 
