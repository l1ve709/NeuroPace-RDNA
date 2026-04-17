#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <nlohmann/json.hpp>
namespace neuropace {
struct DpcIsrMetrics {
    double dpc_latency_us  = 0.0;   
    double dpc_avg_us      = 0.0;   
    double dpc_max_us      = 0.0;   
    double isr_latency_us  = 0.0;   
    double isr_avg_us      = 0.0;
    double isr_max_us      = 0.0;
    uint64_t dpc_count     = 0;     
    uint64_t isr_count     = 0;     
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DpcIsrMetrics,
    dpc_latency_us, dpc_avg_us, dpc_max_us,
    isr_latency_us, isr_avg_us, isr_max_us,
    dpc_count, isr_count)
struct GpuMetrics {
    uint32_t gpu_clock_mhz     = 0;
    uint32_t mem_clock_mhz     = 0;
    uint32_t gpu_temp_c        = 0;
    uint32_t hotspot_temp_c    = 0;
    uint32_t gpu_tgp_w         = 0;
    uint64_t vram_used_mb      = 0;
    uint64_t vram_total_mb     = 0;
    double   gpu_utilization_pct = 0.0;
    uint32_t fan_speed_rpm     = 0;
    uint32_t fps               = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GpuMetrics,
    gpu_clock_mhz, mem_clock_mhz, gpu_temp_c, hotspot_temp_c,
    gpu_tgp_w, vram_used_mb, vram_total_mb, gpu_utilization_pct,
    fan_speed_rpm, fps)
struct TelemetryFrame {
    uint64_t     timestamp_us = 0;    
    uint64_t     sequence_id  = 0;    
    DpcIsrMetrics dpc_isr;
    GpuMetrics    gpu;
    double       frame_time_ms = 0.0;
    double       fps_instant   = 0.0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TelemetryFrame,
    timestamp_us, sequence_id, dpc_isr, gpu, frame_time_ms, fps_instant)
template<std::size_t WindowSize = 1000>
class RollingStats {
public:
    void Push(double value) noexcept {
        m_buffer[m_head] = value;
        m_head = (m_head + 1) % WindowSize;
        if (m_count < WindowSize) ++m_count;
    }
    [[nodiscard]] double Latest() const noexcept {
        if (m_count == 0) return 0.0;
        std::size_t idx = (m_head == 0) ? (WindowSize - 1) : (m_head - 1);
        return m_buffer[idx];
    }
    [[nodiscard]] double Average() const noexcept {
        if (m_count == 0) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < m_count; ++i)
            sum += m_buffer[i];
        return sum / static_cast<double>(m_count);
    }
    [[nodiscard]] double Max() const noexcept {
        if (m_count == 0) return 0.0;
        double max_val = m_buffer[0];
        for (std::size_t i = 1; i < m_count; ++i)
            max_val = (std::max)(max_val, m_buffer[i]);
        return max_val;
    }
    [[nodiscard]] std::size_t Count() const noexcept { return m_count; }
    void Reset() noexcept {
        m_head = 0;
        m_count = 0;
        m_buffer.fill(0.0);
    }
private:
    std::array<double, WindowSize> m_buffer{};
    std::size_t m_head  = 0;
    std::size_t m_count = 0;
};
inline uint64_t GetTimestampMicroseconds() noexcept {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - start).count()
    );
}
} 
