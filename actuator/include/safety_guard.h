#pragma once
#include "action_types.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <chrono>
#include <vector>
#include <shared_mutex>
#include <fstream>
namespace neuropace {
struct AuditEntry {
    uint64_t    timestamp_us;
    std::string action_type;
    DWORD       target_pid;
    bool        approved;
    std::string reason;      
    double      confidence;
};
struct SafetyConfig {
    uint32_t max_actions_per_second  = 10;     
    uint32_t max_thread_priority     = 2;      
    uint32_t max_tgp_boost_w         = 50;     
    double   min_confidence          = 0.2;    
    uint32_t rollback_timeout_ms     = 5000;   
    bool     enable_audit_log        = true;
    std::string audit_log_path       = "neuropace_audit.log";
};
class SafetyGuard {
public:
    explicit SafetyGuard(const SafetyConfig& config = {});
    ~SafetyGuard();
    SafetyGuard(const SafetyGuard&) = delete;
    SafetyGuard& operator=(const SafetyGuard&) = delete;
    [[nodiscard]] bool ValidateAction(const ActionCommand& cmd, DWORD targetPid);
    [[nodiscard]] bool ValidateProcessHandle(HANDLE handle);
    [[nodiscard]] bool ValidateThreadHandle(HANDLE handle);
    void LogAction(const ActionCommand& cmd, DWORD targetPid, bool executed,
                   const std::string& details = "");
    [[nodiscard]] bool IsRateLimited();
    [[nodiscard]] uint64_t GetApprovedCount() const noexcept { return m_approvedCount; }
    [[nodiscard]] uint64_t GetRejectedCount() const noexcept { return m_rejectedCount; }
    [[nodiscard]] const SafetyConfig& GetConfig() const noexcept { return m_config; }
private:
    static constexpr DWORD kAllowedProcessAccess =
        PROCESS_SET_INFORMATION
        | PROCESS_QUERY_INFORMATION
        | PROCESS_QUERY_LIMITED_INFORMATION;
    static constexpr DWORD kForbiddenProcessAccess =
        PROCESS_VM_READ
        | PROCESS_VM_WRITE
        | PROCESS_VM_OPERATION
        | PROCESS_CREATE_THREAD
        | PROCESS_SUSPEND_RESUME;
    static constexpr DWORD kAllowedThreadAccess =
        THREAD_SET_INFORMATION
        | THREAD_QUERY_INFORMATION
        | THREAD_QUERY_LIMITED_INFORMATION;
    static constexpr DWORD kForbiddenThreadAccess =
        THREAD_SUSPEND_RESUME
        | THREAD_GET_CONTEXT
        | THREAD_SET_CONTEXT;
    void WriteAuditEntry(const AuditEntry& entry);
    SafetyConfig m_config;
    mutable std::shared_mutex m_mutex;
    uint64_t m_approvedCount  = 0;
    uint64_t m_rejectedCount  = 0;
    std::vector<std::chrono::steady_clock::time_point> m_actionTimestamps;
    std::ofstream m_auditFile;
};
} 
