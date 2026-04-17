#include "action_types.h"
#include "safety_guard.h"
#include "ipc_action_subscriber.h"
#include "process_scheduler.h"
#include "adlx_actuator.h"
#include <windows.h>
#include <iostream>
#include <format>
#include <csignal>
#include <atomic>
static std::atomic<bool> g_shutdown{false};
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            std::cout << "\n[MAIN] Shutdown signal received...\n";
            g_shutdown.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}
static void PrintBanner() {
    std::cout << "NeuroPace RDNA Actuator & Scheduler v0.1.0\n";
}
int main() {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    PrintBanner();
    neuropace::SafetyConfig safetyConfig;
    safetyConfig.max_actions_per_second = 10;
    safetyConfig.max_thread_priority    = 2;   
    safetyConfig.max_tgp_boost_w        = 50;
    safetyConfig.min_confidence         = 0.2;
    safetyConfig.rollback_timeout_ms    = 5000;
    safetyConfig.enable_audit_log       = true;
    neuropace::SafetyGuard guard(safetyConfig);
    std::cout << "[MAIN] Safety Guard initialized\n";
    neuropace::SchedulerConfig schedConfig;
    schedConfig.rollback_timeout_ms = safetyConfig.rollback_timeout_ms;
    schedConfig.max_thread_priority = safetyConfig.max_thread_priority;
    neuropace::ProcessScheduler scheduler(guard, schedConfig);
    if (!scheduler.Initialize()) {
        std::cerr << "[MAIN] Failed to initialize scheduler\n";
        return 1;
    }
    neuropace::TgpConfig tgpConfig;
    tgpConfig.max_boost_w        = safetyConfig.max_tgp_boost_w;
    tgpConfig.revert_timeout_ms  = safetyConfig.rollback_timeout_ms;
    tgpConfig.default_tgp_w      = 300;
    tgpConfig.absolute_max_tgp_w = 400;
    neuropace::AdlxActuator tgpActuator(tgpConfig);
    tgpActuator.Initialize();
    neuropace::ActionSubscriberConfig subConfig;
    subConfig.pipe_name = L"\\\\.\\pipe\\neuropace-action";
    neuropace::IpcActionSubscriber subscriber(subConfig);
    subscriber.Start();
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  Press Ctrl+C to stop the Actuator service\n";
    std::cout << "============================================================\n\n";
    uint64_t actionsExecuted = 0;
    uint64_t loopIterations  = 0;
    auto lastGameScan = std::chrono::steady_clock::now();
    auto lastStatusPrint = lastGameScan;
    while (!g_shutdown.load()) {
        ++loopIterations;
        auto now = std::chrono::steady_clock::now();
        if (!scheduler.IsAttached()) {
            if (now - lastGameScan >= std::chrono::seconds(2)) {
                scheduler.AttachToProcess();
                lastGameScan = now;
            }
        }
        if (scheduler.ShouldRollback()) {
            scheduler.RestoreOriginalState();
        }
        tgpActuator.CheckAndRevertTimeout();
        neuropace::ActionCommand cmd;
        while (subscriber.TryPopCommand(cmd)) {
            if (cmd.action == neuropace::ActionType::NO_ACTION) {
                continue;
            }
            if (!guard.ValidateAction(cmd, scheduler.GetTargetPid())) {
                guard.LogAction(cmd, scheduler.GetTargetPid(), false, "Rejected by SafetyGuard");
                continue;
            }
            bool success = false;
            std::string details;
            switch (cmd.action) {
                case neuropace::ActionType::BOOST_TGP: {
                    success = tgpActuator.ApplyBoost(cmd.params.tgp_boost_w);
                    details = std::format("TGP +{}W", cmd.params.tgp_boost_w);
                    break;
                }
                case neuropace::ActionType::REBALANCE_THREADS: {
                    if (scheduler.IsAttached()) {
                        int priority = neuropace::ThreadPriorityFromString(
                            cmd.params.thread_priority
                        );
                        success = scheduler.RebalanceThreads(priority);
                        details = std::format("Priority={}", cmd.params.thread_priority);
                    } else {
                        details = "No game process attached";
                    }
                    break;
                }
                case neuropace::ActionType::BOOST_AND_REBALANCE: {
                    bool tgpOk = tgpActuator.ApplyBoost(cmd.params.tgp_boost_w);
                    bool schedOk = false;
                    if (scheduler.IsAttached()) {
                        int priority = neuropace::ThreadPriorityFromString(
                            cmd.params.thread_priority
                        );
                        schedOk = scheduler.RebalanceThreads(priority);
                    }
                    success = tgpOk || schedOk;
                    details = std::format("TGP +{}W, Priority={}, sched={}",
                                          cmd.params.tgp_boost_w,
                                          cmd.params.thread_priority,
                                          schedOk ? "OK" : "SKIP");
                    break;
                }
                default:
                    break;
            }
            guard.LogAction(cmd, scheduler.GetTargetPid(), success, details);
            if (success) ++actionsExecuted;
        }
        if (now - lastStatusPrint >= std::chrono::seconds(10)) {
            auto stats = subscriber.GetStats();
            auto tgpState = tgpActuator.GetState();
            std::cout << std::format(
                "[STATUS] cmds={} | executed={} | approved={} | rejected={} | "
                "game={} | TGP={}W{}\n",
                stats.commands_received,
                actionsExecuted,
                guard.GetApprovedCount(),
                guard.GetRejectedCount(),
                scheduler.IsAttached() ? "attached" : "scanning",
                tgpState.current_tgp_w,
                tgpState.boost_active ? " (BOOSTED)" : ""
            );
            lastStatusPrint = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "\n[MAIN] Shutting down...\n";
    scheduler.Detach();       
    tgpActuator.RevertToDefault();
    subscriber.Stop();
    std::cout << "\n============================================================\n";
    std::cout << "  NeuroPace RDNA Actuator -- Session Summary\n";
    std::cout << "============================================================\n";
    std::cout << std::format("  Actions executed:  {}\n", actionsExecuted);
    std::cout << std::format("  Actions approved:  {}\n", guard.GetApprovedCount());
    std::cout << std::format("  Actions rejected:  {}\n", guard.GetRejectedCount());
    std::cout << "============================================================\n";
    return 0;
}
