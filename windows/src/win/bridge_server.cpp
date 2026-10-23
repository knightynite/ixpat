#include "win/bridge_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

#include "win/log.h"

namespace ixpat::win {
namespace {
constexpr DWORD kReadTimeoutMs = 2000;
constexpr int kMaxRequest = 20000;  // headers 16 KB + body 8 KB, rounded
}  // namespace

BridgeServer::BridgeServer() = default;
BridgeServer::~BridgeServer() { stop(); }

bool BridgeServer::start(std::uint16_t port, std::string token) {
    if (thread_.joinable()) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 8) != 0) {
        debugLog("bridge: listener failed on 127.0.0.1:" + std::to_string(port) + ", error " + std::to_string(WSAGetLastError()));
        closesocket(s);
        return false;
    }
    listener_ = static_cast<std::uintptr_t>(s);
    port_ = port;
    token_ = std::move(token);
    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    debugLog("bridge: listening on 127.0.0.1:" + std::to_string(port));
    return true;
}

void BridgeServer::stop() {
    if (!thread_.joinable()) return;
    stopping_ = true;
    closesocket(static_cast<SOCKET>(listener_));  // unblocks accept()
    thread_.join();
    listener_ = ~std::uintptr_t{0};
    running_ = false;
}

void BridgeServer::loop() {
    BridgeHandler handler;
    handler.token = token_;
    handler.waveformsJson = [this] { return waveformsJson ? waveformsJson() : std::string("[]"); };
    handler.onPlay = [this](std::string_view name, int intensity) {
        if (onPlay) onPlay(std::string(name), intensity);
    };
    for (;;) {
        sockaddr_in peer{};
        int len = sizeof peer;
        SOCKET c = accept(static_cast<SOCKET>(listener_), reinterpret_cast<sockaddr*>(&peer), &len);
        if (c == INVALID_SOCKET) {
            if (stopping_) break;
            continue;
        }
        DWORD timeout = kReadTimeoutMs;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        std::string buf;
        HttpRequest req;
        ParseState state = ParseState::incomplete;
        char chunk[4096];
        while (state == ParseState::incomplete && buf.size() < static_cast<std::size_t>(kMaxRequest)) {
            const int n = recv(c, chunk, sizeof chunk, 0);
            if (n <= 0) break;
            buf.append(chunk, static_cast<std::size_t>(n));
            state = parseHttpRequest(buf, req);
        }
        HttpResponse resp = state == ParseState::complete ? handler.handle(req) : HttpResponse{400, R"({"error":"bad request"})"};
        const std::string out = serializeResponse(resp);
        send(c, out.data(), static_cast<int>(out.size()), 0);
        shutdown(c, SD_BOTH);
        closesocket(c);
    }
}

}  // namespace ixpat::win
