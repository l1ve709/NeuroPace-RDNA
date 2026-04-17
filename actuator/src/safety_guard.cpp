#include "safety_guard.h"
#include <iostream>
#include <format>
#include <chrono>
#include <algorithm>
namespace neuropace {
SafetyGuard::SafetyGuard(const SafetyConfig& config)
    : m_config(config)
{
    if (m_config.enable_audit_log) {
        m_auditFile.open(m_config.audit_log_path, std::ios::app);
        if (m_auditFile.is_open()) {
            m_auditFile << "\n--- NeuroPace RDNA Audit Log Session ---\n";
            m_auditFile.flush();
        }
    }
}
SafetyGuard::~SafetyGuard() {
    if (m_auditFile.is_open()) {
        m_auditFile << "--- Session End ---\n";
        m_auditFile.close();
    }
}
bool SafetyGuard::ValidateAction(const ActionCommand& cmd, DWORD targetPid) {
    std::unique_lock lock(m_mutex);
    if (IsRateLimited()) {
        ++m_rejectedCount;
        WriteAuditEntry({
            cmd.timestamp_us,
            ActionTypeToString(cmd.action),
            targetPid,
            false,
            "Rate limit exceeded",
            cmd.confidence,
        });
        return false;
    }
    if (cmd.action == ActionType::NO_ACTION) {
        ++m_approvedCount;
        return true;
    }
    if (cmd.confidence < m_config.min_confidence) {
        ++m_rejectedCount;
        WriteAuditEntry({
            cmd.timestamp_us,
            ActionTypeToString(cmd.action),
            targetPid,
            false,
            std::format("Confidence {:.3f} below threshold {:.3f}",
                        cmd.confidence, m_config.min_confidence),
            cmd.confidence,
        });
        return false;
    }
    int requestedPriority = ThreadPriorityFromString(cmd.params.thread_priority);
    if (static_cast<uint32_t>(requestedPriority) > m_config.max_thread_priority) {
        ++m_rejectedCount;
        WriteAuditEntry({
            cmd.timestamp_us,
            ActionTypeToString(cmd.action),
            targetPid,
            false,
            std::format("Thread priority {} exceeds cap {}",
                        requestedPriority, m_config.max_thread_priority),
            cmd.confidence,
        });
        return false;
    }
    if (cmd.params.tgp_boost_w > static_cast<int>(m_config.max_tgp_boost_w)) {
        ++m_rejectedCount;
        WriteAuditEntry({
            cmd.timestamp_us,
            ActionTypeToString(cmd.action),
            targetPid,
            false,
            std::format("TGP boost {}W exceeds cap {}W",
                        cmd.params.tgp_boost_w, m_config.max_tgp_boost_w),
            cmd.confidence,
        });
        return false;
    }
    if (targetPid == 0) {
        ++m_rejectedCount;
        WriteAuditEntry({
            cmd.timestamp_us,
            ActionTypeToString(cmd.action),
            targetPid,
            false,
            "Target PID is 0 (no game process attached)",
            cmd.confidence,
        });
        return false;
    }
    m_actionTimestamps.push_back(std::chrono::steady_clock::now());
    ++m_approvedCount;
    return true;
}
bool SafetyGuard::ValidateProcessHandle(HANDLE handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    char testByte = 0;
    SIZE_T bytesRead = 0;
    BOOL readResult = ::ReadProcessMemory(handle, reinterpret_cast<LPCVOID>(0x1000),
                                          &testByte, 1, &bytesRead);
    if (readResult != FALSE || bytesRead > 0) {
        std::cerr << "[SAFETY] CRITICAL: Process handle has VM_READ access!\n";
        std::cerr << "[SAFETY] This handle MUST NOT be used. Anti-cheat violation.\n";
        return false;
    }
    DWORD err = ::GetLastError();
    if (err != ERROR_ACCESS_DENIED && err != ERROR_NOACCESS) {
        std::cerr << std::format(
            "[SAFETY] WARNING: ReadProcessMemory returned unexpected error: {}\n", err
        );
    }
    return true;
}
bool SafetyGuard::ValidateThreadHandle(HANDLE handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_CONTROL;
    BOOL ctxResult = ::GetThreadContext(handle, &ctx);
    if (ctxResult != FALSE) {
        std::cerr << "[SAFETY] CRITICAL: Thread handle has GET_CONTEXT access!\n";
        return false;
    }
    return true;
}
bool SafetyGuard::IsRateLimited() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(1);
    std::erase_if(m_actionTimestamps, [&cutoff](const auto& ts) {
        return ts < cutoff;
    });
    return m_actionTimestamps.size() >= m_config.max_actions_per_second;
}
void SafetyGuard::LogAction(const ActionCommand& cmd, DWORD targetPid,
                            bool executed, const std::string& details) {
    WriteAuditEntry({
        cmd.timestamp_us,
        ActionTypeToString(cmd.action),
        targetPid,
        executed,
        details,
        cmd.confidence,
    });
}
void SafetyGuard::WriteAuditEntry(const AuditEntry& entry) {
    if (!m_config.enable_audit_log || !m_auditFile.is_open()) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_info = {};
#if defined(_MSC_VER)
    localtime_s(&tm_info, &time);
#else
    localtime_r(&time, &tm_info);
#endif
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_info);
    m_auditFile << std::format(
        "[{}] action={} pid={} approved={} confidence={:.3f} reason=\"{}\"\n",
        timeBuf,
        entry.action_type,
        entry.target_pid,
        entry.approved ? "YES" : "NO",
        entry.confidence,
        entry.reason
    );
    m_auditFile.flush();
}
} 
