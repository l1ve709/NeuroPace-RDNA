#include "adlx_sensor.h"
#include <iostream>
#include <format>
#include <random>
#include <cmath>
#ifdef NEUROPACE_HAS_ADLX
    #include "ADLX.h"
    #include "IPerformanceMonitoring.h"
    #include "IGPUTuning.h"
    #include "ADLXHelper.h"
    using namespace adlx;
#endif
namespace neuropace {
AdlxSensor::AdlxSensor(const AdlxConfig& config)
    : m_config(config)
{
}
AdlxSensor::~AdlxSensor() {
    Stop();
}
bool AdlxSensor::Initialize() {
#ifdef NEUROPACE_HAS_ADLX
    auto* helper = new ADLXHelper();
    ADLX_RESULT res = helper->Initialize();
    if (ADLX_FAILED(res)) {
        delete helper;
        SetError(std::format("ADLX Initialize failed: error code {}", static_cast<int>(res)));
        m_adlxAvailable = false;
        std::cout << "[ADLX] SDK init failed — falling back to mock data\n";
        return true; 
    }
    IADLXSystem* sys = helper->GetSystemServices();
    if (!sys) {
        delete helper;
        SetError("Failed to get ADLX system services");
        m_adlxAvailable = false;
        return true;
    }
    IADLXPerformanceMonitoringServices* perfSvc = nullptr;
    res = sys->GetPerformanceMonitoringServices(&perfSvc);
    if (ADLX_FAILED(res) || !perfSvc) {
        delete helper;
        SetError("Failed to get ADLX performance monitoring services");
        m_adlxAvailable = false;
        return true;
    }
    IADLXGPUList* gpuList = nullptr;
    res = sys->GetGPUs(&gpuList);
    if (ADLX_FAILED(res) || !gpuList || gpuList->Size() == 0) {
        delete helper;
        SetError("No AMD GPU found via ADLX");
        m_adlxAvailable = false;
        return true;
    }
    IADLXGPU* gpu = nullptr;
    gpuList->At(0, &gpu);
    const char* gpuName = nullptr;
    if (gpu && ADLX_SUCCEEDED(gpu->Name(&gpuName))) {
        m_gpuName = gpuName;
    }
    m_adlxHelper  = helper;
    m_perfService = perfSvc;
    m_targetGpu   = gpu;
    m_adlxAvailable = true;
    std::cout << std::format("[ADLX] Connected to: {}\n", m_gpuName);
#else
    m_adlxAvailable = false;
    m_gpuName = "AMD Radeon RX 7900 XTX (Mock)";
    std::cout << "[ADLX] SDK not available — using mock GPU telemetry\n";
#endif
    return true;
}
bool AdlxSensor::Start() {
    if (m_running.load()) {
        SetError("ADLX sensor already running");
        return false;
    }
    m_running.store(true);
    m_pollThread = std::thread(&AdlxSensor::PollLoop, this);
    std::cout << std::format("[ADLX] Polling started @ {}ms interval\n", m_config.poll_interval_ms);
    return true;
}
void AdlxSensor::Stop() {
    m_running.store(false);
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
#ifdef NEUROPACE_HAS_ADLX
    if (m_adlxHelper) {
        auto* helper = static_cast<ADLXHelper*>(m_adlxHelper);
        helper->Terminate();
        delete helper;
        m_adlxHelper  = nullptr;
        m_perfService = nullptr;
        m_targetGpu   = nullptr;
    }
#endif
    std::cout << "[ADLX] Sensor stopped\n";
}
GpuMetrics AdlxSensor::GetLatestMetrics() const {
    std::shared_lock lock(m_metricsMutex);
    return m_latestMetrics;
}
std::string AdlxSensor::GetGpuName() const {
    return m_gpuName;
}
std::string AdlxSensor::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void AdlxSensor::PollLoop() {
    using clock = std::chrono::steady_clock;
    while (m_running.load()) {
        auto start = clock::now();
        GpuMetrics metrics;
        if (m_adlxAvailable) {
            metrics = ReadAdlxMetrics();
        } else {
            metrics = GenerateMockMetrics();
        }
        {
            std::unique_lock lock(m_metricsMutex);
            m_latestMetrics = metrics;
        }
        auto elapsed = clock::now() - start;
        auto target  = std::chrono::milliseconds(m_config.poll_interval_ms);
        if (elapsed < target) {
            std::this_thread::sleep_for(target - elapsed);
        }
    }
}
GpuMetrics AdlxSensor::ReadAdlxMetrics() {
    GpuMetrics metrics;
#ifdef NEUROPACE_HAS_ADLX
    auto* perfSvc = static_cast<IADLXPerformanceMonitoringServices*>(m_perfService);
    auto* gpu     = static_cast<IADLXGPU*>(m_targetGpu);
    if (!perfSvc || !gpu) return metrics;
    IADLXGPUMetrics* gpuMetrics = nullptr;
    IADLXGPUMetricsList* metricsList = nullptr;
    ADLX_RESULT res = perfSvc->GetCurrentGPUMetrics(gpu, &metricsList);
    if (ADLX_FAILED(res) || !metricsList || metricsList->Size() == 0) {
        return metrics;
    }
    metricsList->At(0, &gpuMetrics);
    if (!gpuMetrics) return metrics;
    adlx_int clockMHz = 0, memClockMHz = 0, tempC = 0, hotspotC = 0;
    adlx_int tgpW = 0, fanRpm = 0;
    adlx_int vramUsedMB = 0;
    adlx_double utilPct = 0.0;
    gpuMetrics->GPUClockSpeed(&clockMHz);
    gpuMetrics->GPUVRAMClockSpeed(&memClockMHz);
    gpuMetrics->GPUTemperature(&tempC);
    gpuMetrics->GPUHotspotTemperature(&hotspotC);
    gpuMetrics->GPUTotalBoardPower(&tgpW);
    gpuMetrics->GPUUsage(&utilPct);
    gpuMetrics->GPUFanSpeed(&fanRpm);
    gpuMetrics->GPUVRAMUsed(&vramUsedMB);
    metrics.gpu_clock_mhz      = static_cast<uint32_t>(clockMHz);
    metrics.mem_clock_mhz      = static_cast<uint32_t>(memClockMHz);
    metrics.gpu_temp_c         = static_cast<uint32_t>(tempC);
    metrics.hotspot_temp_c     = static_cast<uint32_t>(hotspotC);
    metrics.gpu_tgp_w          = static_cast<uint32_t>(tgpW);
    metrics.vram_used_mb       = static_cast<uint64_t>(vramUsedMB);
    adlx_size vramTotal = 0;
    if (ADLX_SUCCEEDED(gpu->TotalVRAM(&vramTotal))) {
        metrics.vram_total_mb = static_cast<uint64_t>(vramTotal);
    } else {
        metrics.vram_total_mb = 24576; 
    }
    metrics.gpu_utilization_pct = utilPct;
    metrics.fan_speed_rpm      = static_cast<uint32_t>(fanRpm);
#endif
    return metrics;
}
GpuMetrics AdlxSensor::GenerateMockMetrics() {
    thread_local std::mt19937 rng(42);
    thread_local uint64_t tick = 0;
    ++tick;
    const double time_s = static_cast<double>(tick) * m_config.poll_interval_ms / 1000.0;
    const double load_cycle = 0.94 + 0.04 * std::sin(time_s * 0.5);
    std::normal_distribution<double> noise(0.0, 0.5);
    std::normal_distribution<double> temp_noise(0.0, 0.3);
    std::normal_distribution<double> power_noise(0.0, 2.0);
    GpuMetrics m;
    m.gpu_utilization_pct = std::clamp(load_cycle * 100.0 + noise(rng), 0.0, 100.0);
    const double base_clock = 2500.0 + 60.0 * std::sin(time_s * 0.3);
    m.gpu_clock_mhz = static_cast<uint32_t>(std::clamp(base_clock + noise(rng) * 10, 2200.0, 2620.0));
    m.mem_clock_mhz = 1250;
    const double temp = 70.0 + 8.0 * std::sin(time_s * 0.1) + temp_noise(rng);
    m.gpu_temp_c     = static_cast<uint32_t>(std::clamp(temp, 55.0, 95.0));
    m.hotspot_temp_c = static_cast<uint32_t>(std::clamp(temp + 12.0 + temp_noise(rng), 60.0, 110.0));
    const double power = 310.0 + 25.0 * std::sin(time_s * 0.4) + power_noise(rng);
    m.gpu_tgp_w = static_cast<uint32_t>(std::clamp(power, 250.0, 355.0));
    m.vram_used_mb  = 8192 + static_cast<uint64_t>(512.0 * std::sin(time_s * 0.05));
    m.vram_total_mb = 24576;  
    m.fan_speed_rpm = static_cast<uint32_t>(800 + (m.gpu_temp_c - 55) * 30);
    return m;
}
void AdlxSensor::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[ADLX] ERROR: " << msg << "\n";
}
} 
