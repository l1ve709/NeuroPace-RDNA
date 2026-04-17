#include "ipc_publisher.h"
#include <iostream>
#include <format>
#include <sddl.h>
namespace neuropace {
IpcPublisher::IpcPublisher(const PipeConfig& config)
    : m_config(config)
{
}
IpcPublisher::~IpcPublisher() {
    Stop();
}
bool IpcPublisher::Start() {
    if (m_running.load()) {
        SetError("Publisher already running");
        return false;
    }
    m_shutdownEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_shutdownEvent) {
        SetError(std::format("CreateEvent failed: error {}", ::GetLastError()));
        return false;
    }
    m_running.store(true);
    m_acceptThread = std::thread(&IpcPublisher::AcceptLoop, this);
    std::wcout << std::format(L"[IPC] Named Pipe server started: {}\n", m_config.pipe_name);
    return true;
}
void IpcPublisher::Stop() {
    m_running.store(false);
    if (m_shutdownEvent) {
        ::SetEvent(m_shutdownEvent);
    }
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    CleanupAllClients();
    if (m_shutdownEvent) {
        ::CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
    }
    std::cout << "[IPC] Publisher stopped\n";
}
uint32_t IpcPublisher::Publish(const TelemetryFrame& frame) {
    nlohmann::json j = frame;
    std::string payload = j.dump() + "\n";
    std::unique_lock lock(m_clientsMutex);
    uint32_t sent = 0;
    for (auto& client : m_clients) {
        if (client.connected) {
            if (WriteToClient(client, payload)) {
                ++client.frames_sent;
                ++sent;
            } else {
                DisconnectClient(client);
            }
        }
    }
    std::erase_if(m_clients, [](const PipeClient& c) {
        return !c.connected && c.handle == INVALID_HANDLE_VALUE;
    });
    m_totalFrames.fetch_add(1);
    return sent;
}
uint32_t IpcPublisher::GetClientCount() const {
    std::shared_lock lock(m_clientsMutex);
    uint32_t count = 0;
    for (const auto& c : m_clients) {
        if (c.connected) ++count;
    }
    return count;
}
std::string IpcPublisher::GetLastError() const {
    std::shared_lock lock(m_errorMutex);
    return m_lastError;
}
void IpcPublisher::AcceptLoop() {
    while (m_running.load()) {
        PipeClient client;
        if (!CreatePipeInstance(client)) {
            if (m_running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        OVERLAPPED ov = {};
        ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ::ConnectNamedPipe(client.handle, &ov);
        DWORD err = ::GetLastError();
        if (!connected) {
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[] = { ov.hEvent, m_shutdownEvent };
                DWORD waitResult = ::WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0 + 1) {
                    ::CancelIo(client.handle);
                    ::CloseHandle(ov.hEvent);
                    ::CloseHandle(client.handle);
                    break;
                }
                DWORD bytesTransferred = 0;
                if (!::GetOverlappedResult(client.handle, &ov, &bytesTransferred, FALSE)) {
                    ::CloseHandle(ov.hEvent);
                    ::CloseHandle(client.handle);
                    continue;
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
            } else {
                ::CloseHandle(ov.hEvent);
                ::CloseHandle(client.handle);
                if (m_running.load()) {
                    SetError(std::format("ConnectNamedPipe failed: error {}", err));
                }
                continue;
            }
        }
        ::CloseHandle(ov.hEvent);
        client.connected = true;
        {
            std::unique_lock lock(m_clientsMutex);
            m_clients.push_back(std::move(client));
        }
        std::cout << std::format("[IPC] Client connected (total: {})\n", GetClientCount());
    }
}
bool IpcPublisher::CreatePipeInstance(PipeClient& client) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    // D:(A;;GA;;;WD) -> Discretionary ACL: Allow Generic All to World (Everyone)
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) {
        SetError(std::format("ConvertStringSecurityDescriptor failed: error {}", ::GetLastError()));
        return false;
    }

    client.handle = ::CreateNamedPipeW(
        m_config.pipe_name.c_str(),
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        m_config.max_instances,
        m_config.buffer_size,
        0,
        m_config.connect_timeout_ms,
        &sa
    );
    LocalFree(sa.lpSecurityDescriptor);

    if (client.handle == INVALID_HANDLE_VALUE) {
        SetError(std::format("CreateNamedPipeW failed: error {}", ::GetLastError()));
        return false;
    }
    client.writeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!client.writeEvent) {
        ::CloseHandle(client.handle);
        client.handle = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}
void IpcPublisher::DisconnectClient(PipeClient& client) {
    if (client.handle != INVALID_HANDLE_VALUE) {
        ::FlushFileBuffers(client.handle);
        ::DisconnectNamedPipe(client.handle);
        ::CloseHandle(client.handle);
        client.handle = INVALID_HANDLE_VALUE;
    }
    if (client.writeEvent != nullptr) {
        ::CloseHandle(client.writeEvent);
        client.writeEvent = nullptr;
    }
    client.connected = false;
}
void IpcPublisher::CleanupAllClients() {
    std::unique_lock lock(m_clientsMutex);
    for (auto& client : m_clients) {
        DisconnectClient(client);
    }
    m_clients.clear();
}
bool IpcPublisher::WriteToClient(PipeClient& client, const std::string& data) {
    if (client.handle == INVALID_HANDLE_VALUE || !client.connected) {
        return false;
    }
    DWORD bytesWritten = 0;
    ::ResetEvent(client.writeEvent);
    client.overlapped.hEvent = client.writeEvent;
    client.overlapped.Offset = 0;
    client.overlapped.OffsetHigh = 0;
    BOOL success = ::WriteFile(
        client.handle,
        data.c_str(),
        static_cast<DWORD>(data.size()),
        &bytesWritten,
        &client.overlapped
    );
    if (!success) {
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD waitResult = ::WaitForSingleObject(client.writeEvent, 50);
            if (waitResult == WAIT_OBJECT_0) {
                ::GetOverlappedResult(client.handle, &client.overlapped, &bytesWritten, FALSE);
                success = TRUE;
            } else if (waitResult == WAIT_TIMEOUT) {
                ::CancelIo(client.handle);
            }
        }
    }
    if (!success || bytesWritten != static_cast<DWORD>(data.size())) {
        return false;
    }
    return true;
}
void IpcPublisher::SetError(const std::string& msg) {
    std::unique_lock lock(m_errorMutex);
    m_lastError = msg;
    std::cerr << "[IPC] ERROR: " << msg << "\n";
}
} 
