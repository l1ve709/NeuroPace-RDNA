#include "etw_collector.h"
#include <iostream>
#include <format>
#include <cstring>
namespace neuropace {
static const GUID PerfInfoGuid = {
    0xCE1DBFB4, 0x137E, 0x4DA6,
    { 0x87, 0xB0, 0x3F, 0x59, 0xAA, 0x10, 0x2C, 0xBC }
};
static constexpr UCHAR kOpcodeDpc         = 66;
static constexpr UCHAR kOpcodeIsr         = 67;
static constexpr UCHAR kOpcodeDpcTimer    = 68;
static constexpr UCHAR kOpcodeIsrChained  = 69;
#pragma pack(push, 1)
struct DpcEventData {
    ULONG64   InitialTime;     
    ULONG_PTR Routine;         
};
struct IsrEventData {
    ULONG64   InitialTime;     
    ULONG_PTR Routine;         
    UCHAR     ReturnValue;     
};
#pragma pack(pop)
static thread_local EtwCollector* g_activeCollector = nullptr;
static double GetQpcFrequencyMHz() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(freq.QuadPart) / 1'000'000.0;
}
static const double kQpcFreqMHz = GetQpcFrequencyMHz();
EtwCollector::EtwCollector(const EtwConfig& config)
    : m_config(config)
{
}
EtwCollector::~EtwCollector() {
    Stop();
}
bool EtwCollector::Start() {
    if (m_running.load()) {
        SetError("ETW session already running");
        return false;
    }
    {
        const ULONG propSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName) + 64;
        std::vector<BYTE> buf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
        props->Wnode.BufferSize = propSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        ::ControlTraceW(0, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
    }
    if (!StartTraceSession()) {
        return false;
    }
    if (!OpenTraceConsumer()) {
        CleanupHandles();
        return false;
    }
    m_running.store(true);
    m_processingThread = std::thread([this]() {
        if (g_activeCollector != nullptr) {
            std::cerr << "[ETW] FATAL: another EtwCollector already active on this thread\n";
            m_running.store(false);
            return;
        }
        g_activeCollector = this;
        TRACEHANDLE handles[] = { m_traceHandle };
        ULONG status = ::ProcessTrace(handles, 1, nullptr, nullptr);
        if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
            SetError(std::format("ProcessTrace failed: error {}", status));
        }
        m_running.store(false);
        g_activeCollector = nullptr;
    });
    std::cout << "[ETW] Kernel trace session started — capturing DPC/ISR events\n";
    return true;
}
void EtwCollector::Stop() {
    if (!m_running.load() && m_sessionHandle == 0) {
        return;
    }
    m_running.store(false);
    if (m_sessionHandle != 0) {
        const ULONG propSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName) + 64;
        std::vector<BYTE> buf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
        props->Wnode.BufferSize = propSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        ::ControlTraceW(m_sessionHandle, nullptr, props, EVENT_TRACE_CONTROL_STOP);
    }
    if (m_processingThread.joinable()) {
        m_processingThread.join();
    }
    if (m_traceHandle != INVALID_PROCESSTRACE_HANDLE) {
        ::CloseTrace(m_traceHandle);
        m_traceHandle = INVALID_PROCESSTRACE_HANDLE;
    }
    m_sessionHandle = 0;
    std::cout << "[ETW] Trace session stopped\n";
}
DpcIsrMetrics EtwCollector::GetLatestMetrics() const {
    std::shared_lock lock(m_metricsMutex);
    DpcIsrMetrics metrics;
    metrics.dpc_latency_us = m_dpcStats.Latest();
    metrics.dpc_avg_us     = m_dpcStats.Average();
    metrics.dpc_max_us     = m_dpcStats.Max();
    metrics.isr_latency_us = m_isrStats.Latest();
    metrics.isr_avg_us     = m_isrStats.Average();
    metrics.isr_max_us     = m_isrStats.Max();
    metrics.dpc_count      = m_dpcCount;
    metrics.isr_count      = m_isrCount;
    return metrics;
}
std::string EtwCollector::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
bool EtwCollector::StartTraceSession() {
    const ULONG propSize = sizeof(EVENT_TRACE_PROPERTIES)
                         + static_cast<ULONG>((wcslen(kSessionName) + 1) * sizeof(wchar_t));
    m_propertiesBuffer.resize(propSize, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_propertiesBuffer.data());
    props->Wnode.BufferSize    = propSize;
    props->Wnode.Flags         = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;   
    props->Wnode.Guid          = SystemTraceControlGuid;
    props->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset    = sizeof(EVENT_TRACE_PROPERTIES);
    props->FlushTimer          = m_config.flush_timer_ms;
    props->BufferSize          = 64;    
    props->MinimumBuffers      = 4;
    props->MaximumBuffers      = 16;
    ULONG enableFlags = 0;
    if (m_config.capture_dpc) enableFlags |= EVENT_TRACE_FLAG_DPC;
    if (m_config.capture_isr) enableFlags |= EVENT_TRACE_FLAG_INTERRUPT;
    props->EnableFlags = enableFlags;
    ULONG status = ::StartTraceW(
        &m_sessionHandle,
        kSessionName,
        props
    );
    if (status == ERROR_ALREADY_EXISTS) {
        ::ControlTraceW(0, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
        std::memset(m_propertiesBuffer.data(), 0, propSize);
        props->Wnode.BufferSize    = propSize;
        props->Wnode.Flags         = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1;
        props->Wnode.Guid          = SystemTraceControlGuid;
        props->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset    = sizeof(EVENT_TRACE_PROPERTIES);
        props->FlushTimer          = m_config.flush_timer_ms;
        props->BufferSize          = 64;
        props->MinimumBuffers      = 4;
        props->MaximumBuffers      = 16;
        props->EnableFlags         = enableFlags;
        status = ::StartTraceW(&m_sessionHandle, kSessionName, props);
    }
    if (status != ERROR_SUCCESS) {
        SetError(std::format("StartTraceW failed: error {} — run as Administrator", status));
        return false;
    }
    return true;
}
bool EtwCollector::OpenTraceConsumer() {
    EVENT_TRACE_LOGFILEW logFile = {};
    logFile.LoggerName           = const_cast<LPWSTR>(kSessionName);
    logFile.ProcessTraceMode     = PROCESS_TRACE_MODE_REAL_TIME
                                 | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback  = &EtwCollector::EventRecordCallback;
    logFile.BufferCallback       = &EtwCollector::BufferCallback;
    m_traceHandle = ::OpenTraceW(&logFile);
    if (m_traceHandle == INVALID_PROCESSTRACE_HANDLE) {
        SetError(std::format("OpenTraceW failed: error {}", ::GetLastError()));
        return false;
    }
    return true;
}
void WINAPI EtwCollector::EventRecordCallback(PEVENT_RECORD pEvent) {
    if (g_activeCollector && g_activeCollector->m_running.load()) {
        g_activeCollector->ProcessEvent(pEvent);
    }
}
ULONG WINAPI EtwCollector::BufferCallback(PEVENT_TRACE_LOGFILE ) {
    return (g_activeCollector && g_activeCollector->m_running.load()) ? TRUE : FALSE;
}
void EtwCollector::ProcessEvent(PEVENT_RECORD pEvent) {
    if (!IsEqualGUID(pEvent->EventHeader.ProviderId, PerfInfoGuid)) {
        return;
    }
    const UCHAR opcode = pEvent->EventHeader.EventDescriptor.Opcode;
    switch (opcode) {
        case kOpcodeDpc:
        case kOpcodeDpcTimer:
            ProcessDpcEvent(pEvent);
            break;
        case kOpcodeIsr:
        case kOpcodeIsrChained:
            ProcessIsrEvent(pEvent);
            break;
        default:
            break;
    }
}
void EtwCollector::ProcessDpcEvent(PEVENT_RECORD pEvent) {
    if (pEvent->UserDataLength < sizeof(DpcEventData)) {
        return;
    }
    const auto* data = static_cast<const DpcEventData*>(pEvent->UserData);
    const int64_t elapsed = pEvent->EventHeader.TimeStamp.QuadPart
                          - static_cast<int64_t>(data->InitialTime);
    if (elapsed <= 0) return;
    const double latency_us = static_cast<double>(elapsed) / kQpcFreqMHz;
    std::unique_lock lock(m_metricsMutex);
    m_dpcStats.Push(latency_us);
    ++m_dpcCount;
}
void EtwCollector::ProcessIsrEvent(PEVENT_RECORD pEvent) {
    if (pEvent->UserDataLength < sizeof(IsrEventData)) {
        return;
    }
    const auto* data = static_cast<const IsrEventData*>(pEvent->UserData);
    const int64_t elapsed = pEvent->EventHeader.TimeStamp.QuadPart
                          - static_cast<int64_t>(data->InitialTime);
    if (elapsed <= 0) return;
    const double latency_us = static_cast<double>(elapsed) / kQpcFreqMHz;
    std::unique_lock lock(m_metricsMutex);
    m_isrStats.Push(latency_us);
    ++m_isrCount;
}
void EtwCollector::CleanupHandles() {
    if (m_traceHandle != INVALID_PROCESSTRACE_HANDLE) {
        ::CloseTrace(m_traceHandle);
        m_traceHandle = INVALID_PROCESSTRACE_HANDLE;
    }
    if (m_sessionHandle != 0) {
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_propertiesBuffer.data());
        ::ControlTraceW(m_sessionHandle, nullptr, props, EVENT_TRACE_CONTROL_STOP);
        m_sessionHandle = 0;
    }
}
void EtwCollector::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[ETW] ERROR: " << msg << "\n";
}
} 
