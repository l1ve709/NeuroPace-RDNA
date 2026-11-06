#include "adlx_actuator.h"
#include <mutex>
namespace neuropace {
AdlxActuator::AdlxActuator(const TgpConfig& config) : m_config(config) {}
AdlxActuator::~AdlxActuator() = default;
bool AdlxActuator::Initialize() {
    SetError("GPU power control unavailable: device-specific percentage tuning is not implemented");
    return false;
}
bool AdlxActuator::ApplyBoost(int32_t) { return false; }
bool AdlxActuator::RevertToDefault() { return true; }
bool AdlxActuator::CheckAndRevertTimeout() { return false; }
TgpState AdlxActuator::GetState() const {
    std::shared_lock lock(m_stateMutex);
    return m_state;
}
bool AdlxActuator::SetTgpLimit(int32_t) { return false; }
std::string AdlxActuator::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void AdlxActuator::SetError(const std::string& message) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = message;
}
}
