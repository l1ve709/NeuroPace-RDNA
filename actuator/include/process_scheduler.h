#pragma once
#include "action_types.h"
#include "safety_guard.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <shared_mutex>
#include <optional>
namespace neuropace {
inline const std::vector<std::wstring> kKnownGameProcesses = {
    L"VALORANT-Win64-Shipping.exe",
    L"RustClient.exe",
    L"cs2.exe",
    L"FortniteClient-Win64-Shipping.exe",
    L"r5apex.exe",                          
    L"overwatch.exe",
    L"destiny2.exe",
    L"EscapeFromTarkov.exe",
};
struct ThreadState {
    DWORD threadId;
    int   originalPriority;
};
struct ProcessSnapshot {
    DWORD                    pid;
    DWORD_PTR                originalAffinity;
    std::vector<ThreadState> threadStates;
    std::chrono::steady_clock::time_point appliedAt;
    bool                     modified = false;
};
struct SchedulerConfig {
    std::wstring target_process_name;      
    DWORD        target_pid = 0;           
    uint32_t     rollback_timeout_ms = 5000;
    uint32_t     max_thread_priority = 2;  
};
struct CpuTopology {
    uint32_t  logical_processor_count = 0;
    DWORD_PTR system_affinity_mask    = 0;
    DWORD_PTR process_affinity_mask   = 0;
};
class ProcessScheduler {
public:
    explicit ProcessScheduler(SafetyGuard& guard, const SchedulerConfig& config = {});
    ~ProcessScheduler();
    ProcessScheduler(const ProcessScheduler&) = delete;
    ProcessScheduler& operator=(const ProcessScheduler&) = delete;
    bool Initialize();
    bool AttachToProcess();
    bool RebalanceThreads(int targetPriority);
    bool RebalanceThreadsToCores(const std::vector<uint32_t>& cores, int priority);
    bool RestoreOriginalState();
    bool ShouldRollback() const;
    [[nodiscard]] bool IsAttached() const noexcept { return m_processHandle != nullptr; }
    [[nodiscard]] DWORD GetTargetPid() const noexcept { return m_targetPid; }
    [[nodiscard]] std::wstring GetTargetName() const { return m_targetName; }
    [[nodiscard]] const CpuTopology& GetTopology() const noexcept { return m_topology; }
    void Detach();
    [[nodiscard]] std::string GetLastError() const;
private:
    std::optional<DWORD> FindProcessByName(const std::wstring& name);
    std::optional<std::pair<DWORD, std::wstring>> ScanForKnownGames();
    std::vector<DWORD> EnumerateProcessThreads(DWORD pid);
    DWORD_PTR CalculateOptimalAffinity();
    bool SaveCurrentState();
    void SetError(const std::string& msg);
    SafetyGuard& m_guard;
    SchedulerConfig m_config;
    HANDLE       m_processHandle = nullptr;
    DWORD        m_targetPid     = 0;
    std::wstring m_targetName;
    CpuTopology m_topology;
    ProcessSnapshot m_snapshot;
    mutable std::shared_mutex m_errorMutex;
    std::string               m_lastError;
};
} 
