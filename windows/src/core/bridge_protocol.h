#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ixpat {

/// The plugin bridge's wire protocol, ported from Bridge.swift: a loopback HTTP/1.1 endpoint where an
/// app plugin can ask for a pulse by waveform name.
///   POST /haptic   {"token":"...","event":"align","intensity":3}
///   GET  /waveforms  -> the device's table
///   GET  /ping
/// Transport (sockets, threads) lives in win/; this file is the parser and the handler.

struct HttpRequest {
    std::string method, path, body;
};

enum class ParseState : std::uint8_t { incomplete, complete, bad };

constexpr std::size_t kBridgeMaxBody = 8192;
constexpr std::size_t kBridgeMaxHeaders = 16384;

/// Feed everything received so far; `complete` fills `out` (the body is exactly Content-Length bytes).
ParseState parseHttpRequest(std::string_view bytes, HttpRequest& out);

struct HttpResponse {
    int status;
    std::string body;
};

struct BridgeHandler {
    std::string token;                                                  // empty = no check
    std::function<std::string()> waveformsJson;                         // "[{"name":...,"ordinal":...,"ms":...}]"
    std::function<void(std::string_view name, int intensity)> onPlay;  // intensity already clamped to 1..4
    HttpResponse handle(const HttpRequest& r) const;
};

/// Status line, JSON content type, CORS headers, exact Content-Length, Connection: close.
std::string serializeResponse(const HttpResponse& r);

}  // namespace ixpat
