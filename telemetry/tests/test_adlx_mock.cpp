#include "../include/adlx_sensor.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
void test_mock_sensor_lifecycle() {
    neuropace::AdlxConfig config;
    config.poll_interval_ms = 50;  
    neuropace::AdlxSensor sensor(config);
    assert(sensor.Initialize());
    assert(!sensor.IsRunning());
    assert(sensor.Start());
    assert(sensor.IsRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto metrics = sensor.GetLatestMetrics();
    assert(metrics.gpu_clock_mhz >= 2200 && metrics.gpu_clock_mhz <= 2620);
    assert(metrics.gpu_temp_c >= 55 && metrics.gpu_temp_c <= 95);
    assert(metrics.gpu_tgp_w >= 250 && metrics.gpu_tgp_w <= 355);
    assert(metrics.vram_total_mb > 0);
    assert(metrics.gpu_utilization_pct >= 0.0 && metrics.gpu_utilization_pct <= 100.0);
    std::cout << "[TEST] Mock metrics look realistic:\n";
    std::cout << "  Clock: " << metrics.gpu_clock_mhz << " MHz\n";
    std::cout << "  Temp:  " << metrics.gpu_temp_c << " °C\n";
    std::cout << "  TGP:   " << metrics.gpu_tgp_w << " W\n";
    std::cout << "  VRAM:  " << metrics.vram_used_mb << "/" << metrics.vram_total_mb << " MB\n";
    std::cout << "  Util:  " << metrics.gpu_utilization_pct << " %\n";
    sensor.Stop();
    assert(!sensor.IsRunning());
    std::cout << "[TEST] test_mock_sensor_lifecycle PASSED\n";
}
void test_gpu_name() {
    neuropace::AdlxSensor sensor;
    sensor.Initialize();
    std::string name = sensor.GetGpuName();
    assert(!name.empty());
    std::cout << "[TEST] GPU Name: " << name << "\n";
    std::cout << "[TEST] test_gpu_name PASSED\n";
}
int main() {
    std::cout << "========================================\n";
    std::cout << "  NeuroPace RDNA — ADLX Mock Tests\n";
    std::cout << "========================================\n\n";
    test_mock_sensor_lifecycle();
    test_gpu_name();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
