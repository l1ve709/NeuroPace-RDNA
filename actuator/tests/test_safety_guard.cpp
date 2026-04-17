#include "../include/safety_guard.h"
#include "../include/action_types.h"
#include <iostream>
#include <cassert>
using namespace neuropace;
void test_no_action_always_passes() {
    SafetyConfig config;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::NO_ACTION;
    cmd.confidence = 0.0;
    assert(guard.ValidateAction(cmd, 1234));
    std::cout << "[TEST] test_no_action_always_passes PASSED\n";
}
void test_reject_low_confidence() {
    SafetyConfig config;
    config.min_confidence = 0.5;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::REBALANCE_THREADS;
    cmd.confidence = 0.3;  
    cmd.params.thread_priority = "NORMAL";
    assert(!guard.ValidateAction(cmd, 1234));
    assert(guard.GetRejectedCount() == 1);
    std::cout << "[TEST] test_reject_low_confidence PASSED\n";
}
void test_reject_excessive_tgp() {
    SafetyConfig config;
    config.max_tgp_boost_w = 30;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::BOOST_TGP;
    cmd.confidence = 0.8;
    cmd.params.tgp_boost_w = 50;  
    assert(!guard.ValidateAction(cmd, 1234));
    std::cout << "[TEST] test_reject_excessive_tgp PASSED\n";
}
void test_reject_excessive_priority() {
    SafetyConfig config;
    config.max_thread_priority = 1;  
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::REBALANCE_THREADS;
    cmd.confidence = 0.9;
    cmd.params.thread_priority = "HIGH";  
    assert(!guard.ValidateAction(cmd, 1234));
    std::cout << "[TEST] test_reject_excessive_priority PASSED\n";
}
void test_reject_zero_pid() {
    SafetyConfig config;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::REBALANCE_THREADS;
    cmd.confidence = 0.9;
    cmd.params.thread_priority = "NORMAL";
    assert(!guard.ValidateAction(cmd, 0));  
    std::cout << "[TEST] test_reject_zero_pid PASSED\n";
}
void test_approve_valid_action() {
    SafetyConfig config;
    config.min_confidence = 0.2;
    config.max_tgp_boost_w = 50;
    config.max_thread_priority = 2;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::BOOST_AND_REBALANCE;
    cmd.confidence = 0.85;
    cmd.params.tgp_boost_w = 20;
    cmd.params.thread_priority = "ABOVE_NORMAL";
    assert(guard.ValidateAction(cmd, 4567));
    assert(guard.GetApprovedCount() == 1);
    std::cout << "[TEST] test_approve_valid_action PASSED\n";
}
void test_rate_limiting() {
    SafetyConfig config;
    config.max_actions_per_second = 3;
    config.min_confidence = 0.0;
    SafetyGuard guard(config);
    ActionCommand cmd;
    cmd.action = ActionType::BOOST_TGP;
    cmd.confidence = 0.9;
    cmd.params.tgp_boost_w = 10;
    assert(guard.ValidateAction(cmd, 1234));
    assert(guard.ValidateAction(cmd, 1234));
    assert(guard.ValidateAction(cmd, 1234));
    assert(!guard.ValidateAction(cmd, 1234));
    std::cout << "[TEST] test_rate_limiting PASSED\n";
}
void test_action_type_conversion() {
    assert(ActionTypeFromString("NO_ACTION") == ActionType::NO_ACTION);
    assert(ActionTypeFromString("BOOST_TGP") == ActionType::BOOST_TGP);
    assert(ActionTypeFromString("REBALANCE_THREADS") == ActionType::REBALANCE_THREADS);
    assert(ActionTypeFromString("BOOST_AND_REBALANCE") == ActionType::BOOST_AND_REBALANCE);
    assert(ActionTypeFromString("INVALID") == ActionType::NO_ACTION);
    assert(ActionTypeToString(ActionType::BOOST_TGP) == "BOOST_TGP");
    assert(ActionTypeToString(ActionType::NO_ACTION) == "NO_ACTION");
    std::cout << "[TEST] test_action_type_conversion PASSED\n";
}
void test_thread_priority_mapping() {
    assert(ThreadPriorityFromString("NORMAL") == 0);
    assert(ThreadPriorityFromString("ABOVE_NORMAL") == 1);
    assert(ThreadPriorityFromString("HIGH") == 2);
    assert(ThreadPriorityFromString("UNKNOWN") == 0);
    std::cout << "[TEST] test_thread_priority_mapping PASSED\n";
}
void test_json_deserialization() {
    nlohmann::json j = {
        {"timestamp_us", 1234567890},
        {"action", "REBALANCE_THREADS"},
        {"confidence", 0.85},
        {"prediction", {
            {"frame_drop_probability", 0.75},
            {"predicted_latency_spike_us", 320.5},
            {"contributing_factors", {"dpc_spike", "thermal_throttle"}}
        }},
        {"params", {
            {"tgp_boost_w", 15},
            {"thread_priority", "ABOVE_NORMAL"}
        }}
    };
    ActionCommand cmd = j.get<ActionCommand>();
    assert(cmd.timestamp_us == 1234567890);
    assert(cmd.action == ActionType::REBALANCE_THREADS);
    assert(cmd.confidence == 0.85);
    assert(cmd.prediction.frame_drop_probability == 0.75);
    assert(cmd.prediction.contributing_factors.size() == 2);
    assert(cmd.params.tgp_boost_w == 15);
    assert(cmd.params.thread_priority == "ABOVE_NORMAL");
    std::cout << "[TEST] test_json_deserialization PASSED\n";
}
int main() {
    std::cout << "========================================\n";
    std::cout << "  NeuroPace RDNA -- Safety Guard Tests\n";
    std::cout << "========================================\n\n";
    test_no_action_always_passes();
    test_reject_low_confidence();
    test_reject_excessive_tgp();
    test_reject_excessive_priority();
    test_reject_zero_pid();
    test_approve_valid_action();
    test_rate_limiting();
    test_action_type_conversion();
    test_thread_priority_mapping();
    test_json_deserialization();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
