#include "ipc_action_subscriber.h"
#include <chrono>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    neuropace::ActionSubscriberConfig config;
    config.pipe_name += L"-test-" + std::to_wstring(::GetCurrentProcessId());
    HANDLE server = ::CreateNamedPipeW(config.pipe_name.c_str(), PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 0, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) return 1;

    neuropace::IpcActionSubscriber subscriber(config);
    if (!subscriber.Start()) {
        ::CloseHandle(server);
        return 2;
    }
    if (!::ConnectNamedPipe(server, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        ::CloseHandle(server);
        subscriber.Stop();
        return 4;
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!subscriber.IsConnected() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    const bool connected = subscriber.IsConnected();
    // Leave the writer idle so the next read can become pending before Stop().
    std::this_thread::sleep_for(100ms);
    subscriber.Stop();
    ::CloseHandle(server);
    return connected && !subscriber.IsConnected() ? 0 : 3;
}
