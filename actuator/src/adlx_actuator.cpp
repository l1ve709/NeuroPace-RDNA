#include "adlx_actuator.h"
#include <iostream>
#include <format>
#include <algorithm>
#ifdef NEUROPACE_HAS_ADLX
    #include "ADLX.h"
    #include "IGPUTuning.h"
    #include "ADLXHelper.h"
    using namespace adlx;
#endif
namespace neuropace {
AdlxActuator::AdlxActuator(const TgpConfig& config)
    : m_config(config)
{
}
AdlxActuator::~AdlxActuator() {
    if (m_state.boost_active) {
        RevertToDefault();
    }
#ifdef NEUROPACE_HAS_ADLX
    if (m_adlxHelper) {
        auto* helper = static_cast<ADLXHelper*>(m_adlxHelper);
        helper->Terminate();
        delete helper;
        m_adlxHelper = nullptr;
    }
#endif
}
bool AdlxActuator::Initialize() {
#ifdef NEUROPACE_HAS_ADLX
    auto* helper = new ADLXHelper();
    ADLX_RESULT res = helper->Initialize();
    if (ADLX_FAILED(res)) {
        delete helper;
        m_adlxAvailable = false;
        std::cout << "[TGP] ADLX init failed -- TGP control disabled (mock)\n";
        return true;
    }
    IADLXSystem* sys = helper->GetSystemServices();
    if (!sys) {
        delete helper;
        m_adlxAvailable = false;
        return true;
    }
    IADLXGPUTuningServices* tuningSvc = nullptr;
    res = sys->GetGPUTuningServices(&tuningSvc);
    if (ADLX_FAILED(res) || !tuningSvc) {
        delete helper;
        m_adlxAvailable = false;
        std::cout << "[TGP] GPU tuning service unavailable\n";
        return true;
    }
    IADLXGPUList* gpuList = nullptr;
    sys->GetGPUs(&gpuList);
    if (!gpuList || gpuList->Size() == 0) {
        delete helper;
        m_adlxAvailable = false;
        return true;
    }
    IADLXGPU* gpu = nullptr;
    gpuList->At(0, &gpu);
    m_adlxHelper    = helper;
    m_tuningService = tuningSvc;
    m_targetGpu     = gpu;
    m_adlxAvailable = true;
    m_state.default_tgp_w = m_config.default_tgp_w;
    m_state.current_tgp_w = m_config.default_tgp_w;
    std::cout << std::format("[TGP] ADLX initialized, default TGP: {}W\n",
                             m_state.default_tgp_w);
#else
    m_adlxAvailable = false;
    m_state.default_tgp_w = m_config.default_tgp_w;
    m_state.current_tgp_w = m_config.default_tgp_w;
    std::cout << "[TGP] ADLX not available -- using mock TGP control\n";
#endif
    return true;
}
bool AdlxActuator::ApplyBoost(int32_t boost_w) {
    boost_w = (std::min)(boost_w, m_config.max_boost_w);
    int32_t target_tgp = m_state.default_tgp_w + boost_w;
    target_tgp = (std::min)(target_tgp, m_config.absolute_max_tgp_w);
    if (!SetTgpLimit(target_tgp)) {
        return false;
    }
    std::unique_lock lock(m_stateMutex);
    m_state.boost_applied_w = boost_w;
    m_state.boost_active    = true;
    m_state.boost_start     = std::chrono::steady_clock::now();
    m_state.current_tgp_w   = target_tgp;
    std::cout << std::format(
        "[TGP] Boost applied: +{}W (total: {}W, max: {}W)\n",
        boost_w, target_tgp, m_config.absolute_max_tgp_w
    );
    return true;
}
bool AdlxActuator::RevertToDefault() {
    if (!m_state.boost_active) return true;
    if (!SetTgpLimit(m_state.default_tgp_w)) {
        return false;
    }
    std::unique_lock lock(m_stateMutex);
    m_state.boost_applied_w = 0;
    m_state.boost_active    = false;
    m_state.current_tgp_w   = m_state.default_tgp_w;
    std::cout << std::format("[TGP] Reverted to default: {}W\n", m_state.default_tgp_w);
    return true;
}
bool AdlxActuator::CheckAndRevertTimeout() {
    std::unique_lock lock(m_stateMutex);
    if (!m_state.boost_active) return false;
    auto elapsed = std::chrono::steady_clock::now() - m_state.boost_start;
    auto timeout = std::chrono::milliseconds(m_config.revert_timeout_ms);
    if (elapsed >= timeout) {
        std::cout << "[TGP] Boost timeout expired -- reverting\n";
        bool ok = SetTgpLimit(m_state.default_tgp_w);
        if (ok) {
            m_state.boost_applied_w = 0;
            m_state.boost_active    = false;
            m_state.current_tgp_w   = m_state.default_tgp_w;
            std::cout << std::format("[TGP] Reverted to default: {}W\n", m_state.default_tgp_w);
        }
        return ok;
    }
    return false;
}
TgpState AdlxActuator::GetState() const {
    std::shared_lock lock(m_stateMutex);
    return m_state;
}
bool AdlxActuator::SetTgpLimit(int32_t tgp_w) {
    if (!m_adlxAvailable) {
        return true;
    }
#ifdef NEUROPACE_HAS_ADLX
    auto* tuningSvc = static_cast<IADLXGPUTuningServices*>(m_tuningService);
    auto* gpu       = static_cast<IADLXGPU*>(m_targetGpu);
    if (!tuningSvc || !gpu) {
        SetError("ADLX handles invalid");
        return false;
    }
    IADLXInterface* tuningIface = nullptr;
    ADLX_RESULT res = tuningSvc->GetManualPowerTuning(gpu, &tuningIface);
    if (ADLX_FAILED(res) || !tuningIface) {
        SetError("Failed to get manual power tuning interface");
        return false;
    }
    auto* powerTuning = static_cast<IADLXManualPowerTuning*>(tuningIface);
    res = powerTuning->SetPowerLimit(tgp_w);
    if (ADLX_FAILED(res)) {
        SetError(std::format("SetPowerLimit failed: {}", static_cast<int>(res)));
        return false;
    }
#endif
    return true;
}
std::string AdlxActuator::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void AdlxActuator::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[TGP] ERROR: " << msg << "\n";
}
} 
