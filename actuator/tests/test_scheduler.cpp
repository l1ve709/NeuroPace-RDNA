#include "../include/process_scheduler.h"
#include <iostream>
#include <cassert>
using namespace neuropace;
void test_scheduler_initialization() {
    SafetyGuard guard;
    ProcessScheduler scheduler(guard);
    assert(scheduler.Initialize());
    assert(!scheduler.IsAttached());
    assert(scheduler.GetTargetPid() == 0);
    auto topo = scheduler.GetTopology();
    assert(topo.logical_processor_count > 0);
    assert(topo.system_affinity_mask != 0);
    std::cout << "[TEST] CPU: " << topo.logical_processor_count
              << " cores, mask: 0x" << std::hex << topo.system_affinity_mask
              << std::dec << "\n";
    std::cout << "[TEST] test_scheduler_initialization PASSED\n";
}
void test_not_attached_rebalance_fails() {
    SafetyGuard guard;
    ProcessScheduler scheduler(guard);
    scheduler.Initialize();
    assert(!scheduler.RebalanceThreads(0));
    std::cout << "[TEST] test_not_attached_rebalance_fails PASSED\n";
}
void test_rollback_not_needed_initially() {
    SafetyGuard guard;
    ProcessScheduler scheduler(guard);
    scheduler.Initialize();
    assert(!scheduler.ShouldRollback());
    std::cout << "[TEST] test_rollback_not_needed_initially PASSED\n";
}
void test_known_game_processes_list() {
    assert(!kKnownGameProcesses.empty());
    assert(kKnownGameProcesses.size() >= 5);
    bool hasValorant = false;
    bool hasRust = false;
    for (const auto& name : kKnownGameProcesses) {
        if (name == L"VALORANT-Win64-Shipping.exe") hasValorant = true;
        if (name == L"RustClient.exe") hasRust = true;
    }
    assert(hasValorant);
    assert(hasRust);
    std::cout << "[TEST] test_known_game_processes_list PASSED\n";
}
void test_detach_without_attach() {
    SafetyGuard guard;
    ProcessScheduler scheduler(guard);
    scheduler.Initialize();
    scheduler.Detach();
    assert(!scheduler.IsAttached());
    std::cout << "[TEST] test_detach_without_attach PASSED\n";
}
int main() {
    std::cout << "========================================\n";
    std::cout << "  NeuroPace RDNA -- Scheduler Tests\n";
    std::cout << "========================================\n\n";
    test_scheduler_initialization();
    test_not_attached_rebalance_fails();
    test_rollback_not_needed_initially();
    test_known_game_processes_list();
    test_detach_without_attach();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
