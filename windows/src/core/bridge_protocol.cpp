#include "core/bridge_protocol.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <nlohmann/json.hpp>

#include "core/haptic_table.h"

namespace ixpat {
namespace {

using json = nlohmann::json;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string errorBody(const char* message) { return json{{"error", message}}.dump(); }

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        default: return "Unknown";
    }
}

}  // namespace

ParseState parseHttpRequest(std::string_view bytes, HttpRequest& out) {
    const auto headerEnd = bytes.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        return bytes.size() > kBridgeMaxHeaders ? ParseState::bad : ParseState::incomplete;
    }
    if (headerEnd > kBridgeMaxHeaders) return ParseState::bad;
    const std::string_view head = bytes.substr(0, headerEnd);
    const auto firstLineEnd = head.find("\r\n");
    const std::string_view requestLine = head.substr(0, firstLineEnd);
    // "METHOD SP PATH SP VERSION"
    const auto sp1 = requestLine.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) return ParseState::bad;
    const auto sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) return ParseState::bad;
    std::string method(requestLine.substr(0, sp1));
    std::string path(requestLine.substr(sp1 + 1, sp2 - sp1 - 1));

    std::size_t length = 0;
    std::string_view rest = firstLineEnd == std::string_view::npos ? std::string_view{} : head.substr(firstLineEnd + 2);
    while (!rest.empty()) {
        auto eol = rest.find("\r\n");
        std::string_view line = rest.substr(0, eol);
        rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 2);
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        if (lower(trim(line.substr(0, colon))) == "content-length") {
            std::string_view v = trim(line.substr(colon + 1));
            long long n = -1;
            auto res = std::from_chars(v.data(), v.data() + v.size(), n);
            if (res.ec != std::errc{} || res.ptr != v.data() + v.size() || n < 0) return ParseState::bad;
            if (static_cast<std::size_t>(n) > kBridgeMaxBody) return ParseState::bad;
            length = static_cast<std::size_t>(n);
        }
    }
    const std::size_t bodyStart = headerEnd + 4;
    if (bytes.size() - bodyStart < length) return ParseState::incomplete;
    out.method = std::move(method);
    out.path = std::move(path);
    out.body = std::string(bytes.substr(bodyStart, length));
    return ParseState::complete;
}

HttpResponse BridgeHandler::handle(const HttpRequest& r) const {
    if (r.method == "OPTIONS") return {204, ""};
    if (r.method == "GET" && r.path == "/ping") return {200, json{{"ok", true}, {"app", "iXpat"}}.dump()};
    if (r.method == "GET" && r.path == "/waveforms") return {200, waveformsJson ? waveformsJson() : "[]"};
    if (r.method == "POST" && r.path == "/haptic") {
        json obj = json::parse(r.body, nullptr, false);
        if (obj.is_discarded() || !obj.is_object()) return {400, errorBody("bad json")};
        if (!token.empty()) {
            auto t = obj.find("token");
            if (t == obj.end() || !t->is_string() || t->get<std::string>() != token) return {401, errorBody("bad token")};
        }
        auto ev = obj.find("event");
        if (ev == obj.end() || !ev->is_string()) return {400, errorBody("missing or bad event")};
        // Bridge.swift: lowercase, at most 32 of [A-Za-z0-9_-], then the alias table.
        std::string name = lower(ev->get<std::string>());
        const bool nameOk = !name.empty() && name.size() <= 32 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
                                return std::isalnum(c) || c == '_' || c == '-';
                            });
        if (!nameOk) return {400, errorBody("missing or bad event")};
        name = canonicalBridgeName(name);
        int intensity = 3;  // Swift: `(obj["intensity"] as? Int) ?? 3`, then clamped to 1...4
        if (auto in = obj.find("intensity"); in != obj.end() && in->is_number_integer()) {
            const double d = in->get<double>();
            intensity = static_cast<int>(std::clamp(d, 1.0, 4.0));
        }
        if (onPlay) onPlay(name, intensity);
        return {200, json{{"ok", true}, {"event", name}, {"intensity", intensity}}.dump()};
    }
    return {404, errorBody("not found")};
}

std::string serializeResponse(const HttpResponse& r) {
    std::string s = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
    s += "Content-Type: application/json\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\n";
    s += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: content-type\r\n";
    s += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nConnection: close\r\n\r\n";
    s += r.body;
    return s;
}

}  // namespace ixpat
