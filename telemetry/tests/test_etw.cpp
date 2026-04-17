#include "../include/etw_collector.h"
#include <iostream>
#include <cassert>
void test_collector_creation() {
    neuropace::EtwConfig config;
    config.capture_dpc = true;
    config.capture_isr = true;
    neuropace::EtwCollector collector(config);
    assert(!collector.IsRunning());
    auto metrics = collector.GetLatestMetrics();
    assert(metrics.dpc_latency_us == 0.0);
    assert(metrics.isr_latency_us == 0.0);
    assert(metrics.dpc_count == 0);
    assert(metrics.isr_count == 0);
    std::cout << "[TEST] test_collector_creation PASSED\n";
}
void test_rolling_stats() {
    neuropace::RollingStats<5> stats;
    assert(stats.Count() == 0);
    assert(stats.Average() == 0.0);
    assert(stats.Max() == 0.0);
    stats.Push(10.0);
    stats.Push(20.0);
    stats.Push(30.0);
    assert(stats.Count() == 3);
    assert(stats.Latest() == 30.0);
    assert(stats.Average() == 20.0);
    assert(stats.Max() == 30.0);
    stats.Push(5.0);
    stats.Push(15.0);
    stats.Push(25.0);  
    assert(stats.Count() == 5);  
    assert(stats.Latest() == 25.0);
    std::cout << "[TEST] test_rolling_stats PASSED\n";
}
void test_timestamp() {
    uint64_t t1 = neuropace::GetTimestampMicroseconds();
    for (volatile int i = 0; i < 100000; ++i) {}
    uint64_t t2 = neuropace::GetTimestampMicroseconds();
    assert(t2 > t1);
    std::cout << "[TEST] test_timestamp PASSED (delta: " << (t2 - t1) << " µs)\n";
}
int main() {
    std::cout << "========================================\n";
    std::cout << "  NeuroPace RDNA — ETW Collector Tests\n";
    std::cout << "========================================\n\n";
    test_collector_creation();
    test_rolling_stats();
    test_timestamp();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
