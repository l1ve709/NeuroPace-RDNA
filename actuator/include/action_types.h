#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace neuropace {
enum class ActionType {
    NO_ACTION,
    RAISE_POWER_LIMIT,
    REBALANCE_THREADS,
    RAISE_POWER_LIMIT_AND_REBALANCE,
};
inline ActionType ActionTypeFromString(const std::string& s) {
    if (s == "RAISE_POWER_LIMIT")            return ActionType::RAISE_POWER_LIMIT;
    if (s == "REBALANCE_THREADS")    return ActionType::REBALANCE_THREADS;
    if (s == "RAISE_POWER_LIMIT_AND_REBALANCE")  return ActionType::RAISE_POWER_LIMIT_AND_REBALANCE;
    return ActionType::NO_ACTION;
}
inline std::string ActionTypeToString(ActionType t) {
    switch (t) {
        case ActionType::RAISE_POWER_LIMIT:            return "RAISE_POWER_LIMIT";
        case ActionType::REBALANCE_THREADS:    return "REBALANCE_THREADS";
        case ActionType::RAISE_POWER_LIMIT_AND_REBALANCE:  return "RAISE_POWER_LIMIT_AND_REBALANCE";
        default:                               return "NO_ACTION";
    }
}
inline int ThreadPriorityFromString(const std::string& s) {
    if (s == "HIGH")         return 2;
    if (s == "ABOVE_NORMAL") return 1;
    return 0;
}
struct PredictionInfo {
    double frame_drop_probability    = 0.0;
    double predicted_latency_spike_us = 0.0;
    std::vector<std::string> contributing_factors;
};
inline void from_json(const nlohmann::json& j, PredictionInfo& p) {
    p.frame_drop_probability     = j.value("frame_drop_probability", 0.0);
    p.predicted_latency_spike_us = j.value("predicted_latency_spike_us", 0.0);
    if (j.contains("contributing_factors") && j["contributing_factors"].is_array()) {
        p.contributing_factors = j["contributing_factors"].get<std::vector<std::string>>();
    }
}
struct ActionParams {
    int         power_limit_delta_pct     = 0;
    std::string thread_priority = "NORMAL";
};
inline void from_json(const nlohmann::json& j, ActionParams& p) {
    p.power_limit_delta_pct     = j.value("power_limit_delta_pct", 0);
    p.thread_priority = j.value("thread_priority", "NORMAL");
}
struct ActionCommand {
    uint64_t       timestamp_us = 0;
    ActionType     action       = ActionType::NO_ACTION;
    double         confidence   = 0.0;
    PredictionInfo prediction;
    ActionParams   params;
    double         inference_time_ms = 0.0;
};
inline void from_json(const nlohmann::json& j, ActionCommand& cmd) {
    cmd.timestamp_us     = j.value("timestamp_us", uint64_t(0));
    cmd.confidence       = j.value("confidence", 0.0);
    cmd.inference_time_ms = j.value("inference_time_ms", 0.0);
    std::string actionStr = j.value("action", "NO_ACTION");
    cmd.action = ActionTypeFromString(actionStr);
    if (j.contains("prediction")) {
        cmd.prediction = j["prediction"].get<PredictionInfo>();
    }
    if (j.contains("params")) {
        cmd.params = j["params"].get<ActionParams>();
    }
}
}
