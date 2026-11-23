#include "ipc_action_subscriber.h"
#include <iostream>
#include <format>
namespace neuropace {
static constexpr DWORD kErrorFileNotFound = 2;
static constexpr DWORD kErrorPipeBusy     = 231;
static constexpr DWORD kErrorBrokenPipe   = 109;
static constexpr DWORD kErrorNoData       = 232;
IpcActionSubscriber::IpcActionSubscriber(const ActionSubscriberConfig& config)
    : m_config(config)
{
}
IpcActionSubscriber::~IpcActionSubscriber() {
    Stop();
}
bool IpcActionSubscriber::Start() {
    if (m_running.load()) return false;
    m_running.store(true);
    m_readThread = std::thread(&IpcActionSubscriber::ReadLoop, this);
    std::wcout << std::format(L"[ACT-SUB] Subscriber started: {}\n", m_config.pipe_name);
    return true;
}
void IpcActionSubscriber::Stop() {
    m_running.store(false);
    if (m_readThread.joinable()) {
        m_readThread.join();
    }
    std::cout << "[ACT-SUB] Subscriber stopped\n";
}
bool IpcActionSubscriber::TryPopCommand(ActionCommand& cmd) {
    std::unique_lock lock(m_queueMutex);
    if (m_commandQueue.empty()) return false;
    cmd = m_commandQueue.front();
    m_commandQueue.pop();
    return true;
}
ActionSubscriberStats IpcActionSubscriber::GetStats() const {
    std::shared_lock lock(m_statsMutex);
    return m_stats;
}
std::string IpcActionSubscriber::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void IpcActionSubscriber::ReadLoop() {
    std::vector<char> buffer(m_config.read_buffer_size);
    HANDLE readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent) {
        SetError(std::format("CreateEvent failed: {}", ::GetLastError()));
        m_running.store(false);
        return;
    }
    while (m_running.load()) {
        if (!m_connected.load()) {
            if (!Connect()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<int>(m_config.reconnect_interval_sec * 1000)
                    )
                );
                continue;
            }
        }
        OVERLAPPED read{};
        read.hEvent = readEvent;
        ::ResetEvent(readEvent);
        DWORD bytesRead = 0;
        BOOL success = ::ReadFile(
            m_pipeHandle,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr,
            &read
        );
        if (success || ::GetLastError() == ERROR_IO_PENDING) {
            if (!success) {
                while (m_running.load() && ::WaitForSingleObject(readEvent, 25) == WAIT_TIMEOUT) {}
                if (!m_running.load()) {
                    ::CancelIoEx(m_pipeHandle, &read);
                }
            }
            // Drain the operation before releasing its stack storage and pipe handle.
            success = ::GetOverlappedResult(m_pipeHandle, &read, &bytesRead, TRUE);
        }
        if (!success || bytesRead == 0) {
            DWORD err = ::GetLastError();
            if (err == kErrorBrokenPipe || err == kErrorNoData) {
                std::cout << "[ACT-SUB] AI Engine pipe disconnected\n";
            } else if (m_running.load()) {
                SetError(std::format("ReadFile error: {}", err));
            }
            Disconnect();
            continue;
        }
        std::string data(buffer.data(), bytesRead);
        {
            std::unique_lock slock(m_statsMutex);
            m_stats.bytes_received += bytesRead;
        }
        auto messages = m_ndjson.Feed(data);
        for (auto& json : messages) {
            try {
                ActionCommand cmd = json.get<ActionCommand>();
                std::unique_lock qlock(m_queueMutex);
                if (m_commandQueue.size() < m_config.max_queue_size) {
                    m_commandQueue.push(std::move(cmd));
                }
                std::unique_lock slock(m_statsMutex);
                ++m_stats.commands_received;
            } catch (const nlohmann::json::exception&) {
                std::unique_lock slock(m_statsMutex);
                ++m_stats.parse_errors;
            }
        }
    }
    Disconnect();
    ::CloseHandle(readEvent);
}
bool IpcActionSubscriber::Connect() {
    m_pipeHandle = ::CreateFileW(
        m_config.pipe_name.c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );
    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        if (err == kErrorFileNotFound) {
        } else if (err == kErrorPipeBusy) {
        } else {
            SetError(std::format("CreateFile error: {}", err));
        }
        return false;
    }
    m_connected.store(true);
    m_ndjson.Reset();
    std::cout << "[ACT-SUB] Connected to AI Engine pipe\n";
    return true;
}
void IpcActionSubscriber::Disconnect() {
    const bool wasConnected = m_connected.exchange(false);
    if (m_pipeHandle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
    }
    m_ndjson.Reset();
    {
        std::unique_lock lock(m_queueMutex);
        m_commandQueue = {};
    }
    if (wasConnected) {
        // Count only real connection losses, not idle disconnects.
        std::unique_lock lock(m_statsMutex);
        ++m_stats.reconnect_count;
    }
}
void IpcActionSubscriber::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[ACT-SUB] ERROR: " << msg << "\n";
}
} 
