#pragma once
#include "telemetry_data.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <vector>
#include <string>
#include <functional>
namespace neuropace {
struct PipeConfig {
    std::wstring pipe_name       = L"\\\\.\\pipe\\neuropace-telemetry";
    uint32_t     max_instances   = 4;        
    uint32_t     buffer_size     = 65536;    
    uint32_t     connect_timeout_ms = 100;   
};
struct PipeClient {
    HANDLE     handle     = INVALID_HANDLE_VALUE;
    HANDLE     writeEvent = nullptr;
    OVERLAPPED overlapped = {};
    bool       connected  = false;
    uint64_t   frames_sent = 0;
};
class IpcPublisher {
public:
    explicit IpcPublisher(const PipeConfig& config = {});
    ~IpcPublisher();
    IpcPublisher(const IpcPublisher&) = delete;
    IpcPublisher& operator=(const IpcPublisher&) = delete;
    bool Start();
    void Stop();
    uint32_t Publish(const TelemetryFrame& frame);
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] uint32_t GetClientCount() const;
    [[nodiscard]] uint64_t GetTotalFramesPublished() const noexcept {
        return m_totalFrames.load();
    }
    [[nodiscard]] std::string GetLastError() const;
private:
    void AcceptLoop();
    bool CreatePipeInstance(PipeClient& client);
    void DisconnectClient(PipeClient& client);
    void CleanupAllClients();
    bool WriteToClient(PipeClient& client, const std::string& data);
    void SetError(const std::string& msg);
    PipeConfig m_config;
    std::thread       m_acceptThread;
    std::atomic<bool> m_running{false};
    mutable std::shared_mutex  m_clientsMutex;
    std::vector<PipeClient>    m_clients;
    std::atomic<uint64_t> m_totalFrames{0};
    mutable std::shared_mutex m_errorMutex;
    std::string               m_lastError;
    HANDLE m_shutdownEvent = nullptr;
};
} 
