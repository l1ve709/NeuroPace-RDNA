#pragma once
#include "telemetry_data.h"
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <functional>
#include <string>
namespace neuropace {
struct EtwConfig {
    bool        capture_dpc    = true;
    bool        capture_isr    = true;
    uint32_t    flush_timer_ms = 1;  
};
class EtwCollector {
public:
    explicit EtwCollector(const EtwConfig& config = {});
    ~EtwCollector();
    EtwCollector(const EtwCollector&) = delete;
    EtwCollector& operator=(const EtwCollector&) = delete;
    bool Start();
    void Stop();
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] DpcIsrMetrics GetLatestMetrics() const;
    [[nodiscard]] std::string GetLastError() const;
private:
    static void WINAPI EventRecordCallback(PEVENT_RECORD pEvent);
    static ULONG WINAPI BufferCallback(PEVENT_TRACE_LOGFILE pLogFile);
    void ProcessEvent(PEVENT_RECORD pEvent);
    void ProcessDpcEvent(PEVENT_RECORD pEvent);
    void ProcessIsrEvent(PEVENT_RECORD pEvent);
    bool StartTraceSession();
    bool OpenTraceConsumer();
    void CleanupHandles();
    void SetError(const std::string& msg);
    EtwConfig m_config;
    TRACEHANDLE m_sessionHandle = 0;
    TRACEHANDLE m_traceHandle   = INVALID_PROCESSTRACE_HANDLE;
    std::thread       m_processingThread;
    std::atomic<bool> m_running{false};
    mutable std::shared_mutex m_metricsMutex;
    RollingStats<1000>        m_dpcStats;
    RollingStats<1000>        m_isrStats;
    uint64_t                  m_dpcCount = 0;
    uint64_t                  m_isrCount = 0;
    mutable std::shared_mutex m_errorMutex;
    std::string               m_lastError;
    std::vector<BYTE> m_propertiesBuffer;
    static constexpr wchar_t kSessionName[] = KERNEL_LOGGER_NAMEW;
};
} 
