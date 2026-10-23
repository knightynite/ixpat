#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "core/bridge_protocol.h"

namespace ixpat::win {

/// The plugin bridge's transport: a Winsock listener on 127.0.0.1:port, one thread, one connection at a
/// time, `Connection: close`. Protocol and policy live in core/bridge_protocol.
class BridgeServer {
public:
    BridgeServer();
    ~BridgeServer();
    BridgeServer(const BridgeServer&) = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    /// Called on the bridge thread; the owner posts to main.
    std::function<void(std::string name, int intensity)> onPlay;
    std::function<std::string()> waveformsJson;

    bool start(std::uint16_t port, std::string token);
    void stop();
    bool running() const { return running_.load(); }
    std::uint16_t port() const { return port_; }

private:
    void loop();
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::uintptr_t listener_ = ~std::uintptr_t{0};  // INVALID_SOCKET
    std::uint16_t port_ = 0;
    std::string token_;
};

}  // namespace ixpat::win
