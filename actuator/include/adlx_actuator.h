#pragma once
#include <cstdint>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <string>
#include <chrono>
namespace neuropace {
struct TgpConfig {
    int32_t  max_boost_w         = 50;     
    uint32_t revert_timeout_ms   = 5000;   
    int32_t  default_tgp_w       = 300;    
    int32_t  absolute_max_tgp_w  = 400;    
};
struct TgpState {
    int32_t  current_tgp_w      = 0;
    int32_t  default_tgp_w      = 0;
    int32_t  boost_applied_w    = 0;
    bool     boost_active       = false;
    std::chrono::steady_clock::time_point boost_start;
};
class AdlxActuator {
public:
    explicit AdlxActuator(const TgpConfig& config = {});
    ~AdlxActuator();
    AdlxActuator(const AdlxActuator&) = delete;
    AdlxActuator& operator=(const AdlxActuator&) = delete;
    bool Initialize();
    bool ApplyBoost(int32_t boost_w);
    bool RevertToDefault();
    bool CheckAndRevertTimeout();
    [[nodiscard]] TgpState GetState() const;
    [[nodiscard]] bool IsAdlxAvailable() const noexcept { return m_adlxAvailable; }
    [[nodiscard]] std::string GetLastError() const;
private:
    bool SetTgpLimit(int32_t tgp_w);
    void SetError(const std::string& msg);
    TgpConfig m_config;
    mutable std::shared_mutex m_stateMutex;
    TgpState m_state;
    bool  m_adlxAvailable = false;
    void* m_adlxHelper    = nullptr;
    void* m_tuningService = nullptr;
    void* m_targetGpu     = nullptr;
    mutable std::shared_mutex m_errorMutex;
    std::string               m_lastError;
};
} 
