#include "process_scheduler.h"
#include <iostream>
#include <format>
#include <algorithm>
namespace neuropace {
class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE h = nullptr) : m_handle(h) {}
    ~ScopedHandle() { Close(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
    HANDLE Get() const noexcept { return m_handle; }
    HANDLE Release() noexcept { auto h = m_handle; m_handle = nullptr; return h; }
    bool IsValid() const noexcept { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    void Close() {
        if (IsValid()) { ::CloseHandle(m_handle); m_handle = nullptr; }
    }
private:
    HANDLE m_handle;
};
ProcessScheduler::ProcessScheduler(SafetyGuard& guard, const SchedulerConfig& config)
    : m_guard(guard)
    , m_config(config)
{
}
ProcessScheduler::~ProcessScheduler() {
    Detach();
}
bool ProcessScheduler::Initialize() {
    SYSTEM_INFO sysInfo{};
    ::GetSystemInfo(&sysInfo);
    m_topology.logical_processor_count = sysInfo.dwNumberOfProcessors;
    DWORD_PTR processAff = 0, systemAff = 0;
    HANDLE currentProcess = ::GetCurrentProcess();
    if (::GetProcessAffinityMask(currentProcess, &processAff, &systemAff)) {
        m_topology.system_affinity_mask  = systemAff;
        m_topology.process_affinity_mask = processAff;
    }
    std::cout << std::format(
        "[SCHED] CPU Topology: {} logical processors, system mask: 0x{:X}\n",
        m_topology.logical_processor_count,
        m_topology.system_affinity_mask
    );
    return true;
}
bool ProcessScheduler::AttachToProcess() {
    if (m_processHandle != nullptr) {
        return true;
    }
    DWORD pid = 0;
    std::wstring name;
    if (m_config.target_pid != 0) {
        pid = m_config.target_pid;
        name = L"<PID:" + std::to_wstring(pid) + L">";
    } else if (!m_config.target_process_name.empty()) {
        auto result = FindProcessByName(m_config.target_process_name);
        if (!result) {
            return false;  
        }
        pid = *result;
        name = m_config.target_process_name;
    } else {
        auto result = ScanForKnownGames();
        if (!result) {
            return false;
        }
        pid  = result->first;
        name = result->second;
    }
    HANDLE hProcess = ::OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE,
        pid
    );
    if (hProcess == nullptr) {
        SetError(std::format("OpenProcess failed for PID {}: error {}",
                             pid, ::GetLastError()));
        return false;
    }
    if (!m_guard.ValidateProcessHandle(hProcess)) {
        ::CloseHandle(hProcess);
        SetError("SafetyGuard REJECTED process handle — possible anti-cheat violation");
        return false;
    }
    m_processHandle = hProcess;
    m_targetPid     = pid;
    m_targetName    = name;
    SaveCurrentState();
    std::wcout << std::format(
        L"[SCHED] Attached to: {} (PID: {})\n", name, pid
    );
    return true;
}
void ProcessScheduler::Detach() {
    if (m_processHandle == nullptr) return;
    if (m_snapshot.modified) {
        RestoreOriginalState();
    }
    ::CloseHandle(m_processHandle);
    m_processHandle = nullptr;
    m_targetPid     = 0;
    m_targetName.clear();
    std::cout << "[SCHED] Detached from process\n";
}
bool ProcessScheduler::RebalanceThreads(int targetPriority) {
    if (m_processHandle == nullptr) {
        SetError("Not attached to any process");
        return false;
    }
    targetPriority = (std::min)(
        targetPriority,
        static_cast<int>(m_config.max_thread_priority)
    );
    DWORD_PTR newAffinity = CalculateOptimalAffinity();
    bool affinityApplied = false;
    if (::SetProcessAffinityMask(m_processHandle, newAffinity)) {
        affinityApplied = true;
    } else {
        // Error 5 = ACCESS_DENIED — game/anti-cheat may protect the process.
        // Gracefully fall back to thread priority only (no affinity change).
        DWORD err = ::GetLastError();
        static uint32_t affinityWarnCount = 0;
        if (affinityWarnCount++ % 30 == 0) {
            std::cerr << std::format(
                "[SCHED] WARN: SetProcessAffinityMask failed (error {}). "
                "Falling back to thread-priority-only mode.\n", err
            );
        }
    }
    auto threadIds = EnumerateProcessThreads(m_targetPid);
    int threadsModified = 0;
    for (DWORD tid : threadIds) {
        ScopedHandle hThread(::OpenThread(
            THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION,
            FALSE,
            tid
        ));
        if (!hThread.IsValid()) continue;
        if (!m_guard.ValidateThreadHandle(hThread.Get())) {
            continue;
        }
        if (::SetThreadPriority(hThread.Get(), targetPriority)) {
            ++threadsModified;
        }
    }
    m_snapshot.modified = true;
    m_snapshot.appliedAt = std::chrono::steady_clock::now();
    if (threadsModified > 0) {
        std::cout << std::format(
            "[SCHED] Rebalanced: affinity={}, priority={}, threads={}/{}\n",
            affinityApplied ? std::format("0x{:X}", newAffinity) : "SKIPPED",
            targetPriority, threadsModified, threadIds.size()
        );
    }
    return true;
}
bool ProcessScheduler::RebalanceThreadsToCores(
    const std::vector<uint32_t>& cores, int priority)
{
    if (m_processHandle == nullptr) {
        SetError("Not attached to any process");
        return false;
    }
    DWORD_PTR mask = 0;
    for (uint32_t core : cores) {
        if (core < 64) {
            mask |= (static_cast<DWORD_PTR>(1) << core);
        }
    }
    mask &= m_topology.system_affinity_mask;
    if (mask == 0) {
        SetError("Computed affinity mask is zero after intersection with system mask");
        return false;
    }
    if (!::SetProcessAffinityMask(m_processHandle, mask)) {
        // Graceful fallback — log as warning, continue with thread priority.
        static uint32_t affinityWarnCount2 = 0;
        if (affinityWarnCount2++ % 30 == 0) {
            std::cerr << std::format(
                "[SCHED] WARN: SetProcessAffinityMask (cores) failed (error {}). "
                "Thread priority will still be applied.\n", ::GetLastError()
            );
        }
    }
    priority = (std::min)(priority, static_cast<int>(m_config.max_thread_priority));
    auto threadIds = EnumerateProcessThreads(m_targetPid);
    for (DWORD tid : threadIds) {
        ScopedHandle hThread(::OpenThread(
            THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION,
            FALSE, tid
        ));
        if (!hThread.IsValid()) continue;
        if (!m_guard.ValidateThreadHandle(hThread.Get())) continue;
        ::SetThreadPriority(hThread.Get(), priority);
    }
    m_snapshot.modified = true;
    m_snapshot.appliedAt = std::chrono::steady_clock::now();
    return true;
}
bool ProcessScheduler::RestoreOriginalState() {
    if (m_processHandle == nullptr || !m_snapshot.modified) {
        return false;
    }
    if (m_snapshot.originalAffinity != 0) {
        ::SetProcessAffinityMask(m_processHandle, m_snapshot.originalAffinity);
    }
    for (const auto& ts : m_snapshot.threadStates) {
        ScopedHandle hThread(::OpenThread(
            THREAD_SET_INFORMATION, FALSE, ts.threadId
        ));
        if (hThread.IsValid()) {
            ::SetThreadPriority(hThread.Get(), ts.originalPriority);
        }
    }
    m_snapshot.modified = false;
    std::cout << "[SCHED] Original process state restored\n";
    return true;
}
bool ProcessScheduler::ShouldRollback() const {
    if (!m_snapshot.modified) return false;
    auto elapsed = std::chrono::steady_clock::now() - m_snapshot.appliedAt;
    auto timeout = std::chrono::milliseconds(m_config.rollback_timeout_ms);
    return elapsed >= timeout;
}
bool ProcessScheduler::SaveCurrentState() {
    if (m_processHandle == nullptr) return false;
    DWORD_PTR processAff = 0, systemAff = 0;
    if (::GetProcessAffinityMask(m_processHandle, &processAff, &systemAff)) {
        m_snapshot.originalAffinity = processAff;
    }
    m_snapshot.threadStates.clear();
    auto threadIds = EnumerateProcessThreads(m_targetPid);
    for (DWORD tid : threadIds) {
        ScopedHandle hThread(::OpenThread(
            THREAD_QUERY_INFORMATION, FALSE, tid
        ));
        if (hThread.IsValid()) {
            int priority = ::GetThreadPriority(hThread.Get());
            if (priority != THREAD_PRIORITY_ERROR_RETURN) {
                m_snapshot.threadStates.push_back({tid, priority});
            }
        }
    }
    m_snapshot.pid = m_targetPid;
    m_snapshot.modified = false;
    std::cout << std::format(
        "[SCHED] State saved: affinity=0x{:X}, {} threads\n",
        m_snapshot.originalAffinity,
        m_snapshot.threadStates.size()
    );
    return true;
}
std::optional<DWORD> ProcessScheduler::FindProcessByName(const std::wstring& name) {
    ScopedHandle hSnap(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!hSnap.IsValid()) return std::nullopt;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(hSnap.Get(), &pe)) return std::nullopt;
    do {
        if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
            return pe.th32ProcessID;
        }
    } while (::Process32NextW(hSnap.Get(), &pe));
    return std::nullopt;
}
std::optional<std::pair<DWORD, std::wstring>> ProcessScheduler::ScanForKnownGames() {
    ScopedHandle hSnap(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!hSnap.IsValid()) return std::nullopt;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(hSnap.Get(), &pe)) return std::nullopt;
    do {
        for (const auto& gameName : kKnownGameProcesses) {
            if (_wcsicmp(pe.szExeFile, gameName.c_str()) == 0) {
                return std::make_pair(pe.th32ProcessID, gameName);
            }
        }
    } while (::Process32NextW(hSnap.Get(), &pe));
    return std::nullopt;
}
std::vector<DWORD> ProcessScheduler::EnumerateProcessThreads(DWORD pid) {
    std::vector<DWORD> threads;
    ScopedHandle hSnap(::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!hSnap.IsValid()) return threads;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (!::Thread32First(hSnap.Get(), &te)) return threads;
    do {
        if (te.th32OwnerProcessID == pid) {
            threads.push_back(te.th32ThreadID);
        }
    } while (::Thread32Next(hSnap.Get(), &te));
    return threads;
}
DWORD_PTR ProcessScheduler::CalculateOptimalAffinity() {
    DWORD_PTR optimal = m_topology.system_affinity_mask;
    if (m_topology.logical_processor_count > 4) {
        optimal &= ~static_cast<DWORD_PTR>(1);  
    }
    int count = 0;
    DWORD_PTR temp = optimal;
    while (temp) { count += (temp & 1); temp >>= 1; }
    if (count < 2) {
        optimal = m_topology.system_affinity_mask;
    }
    return optimal;
}
std::string ProcessScheduler::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void ProcessScheduler::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[SCHED] ERROR: " << msg << "\n";
}
} 
