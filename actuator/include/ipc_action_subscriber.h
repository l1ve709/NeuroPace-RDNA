#pragma once
#include "action_types.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <string>
#include <queue>
#include <functional>
namespace neuropace {
class NdjsonReader {
public:
    std::vector<nlohmann::json> Feed(const std::string& data) {
        m_buffer += data;
        std::vector<nlohmann::json> messages;
        std::size_t pos;
        while ((pos = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, pos);
            m_buffer.erase(0, pos + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;
            try {
                messages.push_back(nlohmann::json::parse(line));
            } catch (const nlohmann::json::exception&) {
            }
        }
        if (m_buffer.size() > 1'000'000) {
            m_buffer.clear();
        }
        return messages;
    }
    void Reset() { m_buffer.clear(); }
private:
    std::string m_buffer;
};
struct ActionSubscriberConfig {
    std::wstring pipe_name           = L"\\\\.\\pipe\\neuropace-action";
    uint32_t     read_buffer_size    = 65536;
    double       reconnect_interval_sec = 1.0;
    uint32_t     max_queue_size      = 100;
};
struct ActionSubscriberStats {
    uint64_t commands_received  = 0;
    uint64_t bytes_received     = 0;
    uint32_t reconnect_count    = 0;
    uint32_t parse_errors       = 0;
};
class IpcActionSubscriber {
public:
    explicit IpcActionSubscriber(const ActionSubscriberConfig& config = {});
    ~IpcActionSubscriber();
    IpcActionSubscriber(const IpcActionSubscriber&) = delete;
    IpcActionSubscriber& operator=(const IpcActionSubscriber&) = delete;
    bool Start();
    void Stop();
    bool TryPopCommand(ActionCommand& cmd);
    [[nodiscard]] bool IsConnected() const noexcept { return m_connected.load(); }
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] ActionSubscriberStats GetStats() const;
    [[nodiscard]] std::string GetLastError() const;
private:
    void ReadLoop();
    bool Connect();
    void Disconnect();
    void SetError(const std::string& msg);
    ActionSubscriberConfig m_config;
    std::thread       m_readThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    HANDLE m_pipeHandle = INVALID_HANDLE_VALUE;
    NdjsonReader m_ndjson;
    mutable std::shared_mutex      m_queueMutex;
    std::queue<ActionCommand>      m_commandQueue;
    mutable std::shared_mutex      m_statsMutex;
    ActionSubscriberStats          m_stats;
    mutable std::shared_mutex      m_errorMutex;
    std::string                    m_lastError;
};
} 
