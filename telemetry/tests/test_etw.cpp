#include "../include/etw_collector.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <string_view>
#include <thread>
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
int main(int argc, char** argv) {
    if (argc > 1) {
        neuropace::EtwCollector collector;
        if (collector.Start()) {
            if (std::string_view(argv[1]) == "--capture") {
                using namespace std::chrono_literals;
                const auto deadline = std::chrono::steady_clock::now() + 6s;
                auto metrics = collector.GetLatestMetrics();
                while (collector.IsRunning() && (metrics.dpc_count == 0 || metrics.isr_count == 0) &&
                       std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(100ms);
                    metrics = collector.GetLatestMetrics();
                }
                collector.Stop();
                std::cout << "DPC events: " << metrics.dpc_count
                          << ", ISR events: " << metrics.isr_count << '\n';
                return metrics.dpc_count > 0 && metrics.dpc_max_us > 0 &&
                       metrics.isr_count > 0 && metrics.isr_max_us > 0 ? 0 : 1;
            }
            collector.Stop();
            return 0;
        }
        const auto error = collector.GetLastError();
        std::cerr << error << '\n';
        return error.starts_with("StartTraceW failed: error 5 ") ||
               error.starts_with("StartTraceW failed: error 1314 ") ? 77 : 1;
    }
    std::cout << "========================================\n";
    std::cout << "  NeuroPace RDNA — ETW Collector Tests\n";
    std::cout << "========================================\n\n";
    test_collector_creation();
    test_rolling_stats();
    test_timestamp();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
