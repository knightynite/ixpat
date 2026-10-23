#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
#include <vector>

#include "core/bridge_protocol.h"

using ixpat::BridgeHandler;
using ixpat::HttpRequest;
using ixpat::ParseState;

namespace {

struct Played {
    std::string name;
    int intensity;
};

BridgeHandler handler(std::vector<Played>& played, std::string token = "secret") {
    BridgeHandler h;
    h.token = std::move(token);
    h.waveformsJson = [] { return std::string(R"([{"name":"click","ordinal":10,"ms":11}])"); };
    h.onPlay = [&played](std::string_view name, int intensity) { played.push_back({std::string(name), intensity}); };
    return h;
}

HttpRequest req(std::string method, std::string path, std::string body = {}) {
    return HttpRequest{std::move(method), std::move(path), std::move(body)};
}

}  // namespace

TEST_CASE("bridge: GET /ping answers ok with the app name") {
    std::vector<Played> played;
    auto r = handler(played).handle(req("GET", "/ping"));
    CHECK(r.status == 200);
    CHECK(nlohmann::json::parse(r.body) == nlohmann::json({{"ok", true}, {"app", "iXpat"}}));
}

TEST_CASE("bridge: GET /waveforms returns the device table verbatim") {
    std::vector<Played> played;
    auto r = handler(played).handle(req("GET", "/waveforms"));
    CHECK(r.status == 200);
    CHECK(r.body == R"([{"name":"click","ordinal":10,"ms":11}])");
}

TEST_CASE("bridge: POST /haptic with the token plays, clamps intensity and lowercases nothing") {
    std::vector<Played> played;
    auto h = handler(played);
    auto r = h.handle(req("POST", "/haptic", R"({"token":"secret","event":"align","intensity":9})"));
    CHECK(r.status == 200);
    REQUIRE(played.size() == 1);
    CHECK(played[0].name == "align");
    CHECK(played[0].intensity == 4);
    auto j = nlohmann::json::parse(r.body);
    CHECK(j["ok"] == true);
    CHECK(j["event"] == "align");
    CHECK(j["intensity"] == 4);
    h.handle(req("POST", "/haptic", R"({"token":"secret","event":"click"})"));  // default intensity 3
    REQUIRE(played.size() == 2);
    CHECK(played[1].intensity == 3);
    h.handle(req("POST", "/haptic", R"({"token":"secret","event":"click","intensity":0})"));  // floor 1
    CHECK(played[2].intensity == 1);
    h.handle(req("POST", "/haptic", R"({"token":"secret","event":"snap_2-x","intensity":2})"));  // allowed chars
    CHECK(played.size() == 4);
}

TEST_CASE("bridge: the token is required unless empty") {
    std::vector<Played> played;
    auto h = handler(played);
    CHECK(h.handle(req("POST", "/haptic", R"({"event":"align"})")).status == 401);
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"wrong","event":"align"})")).status == 401);
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"","event":"align"})")).status == 401);
    CHECK(played.empty());
    auto open = handler(played, "");
    CHECK(open.handle(req("POST", "/haptic", R"({"event":"align"})")).status == 200);
    CHECK(played.size() == 1);
}

TEST_CASE("bridge: bad requests") {
    std::vector<Played> played;
    auto h = handler(played);
    CHECK(h.handle(req("POST", "/haptic", "{nope")).status == 400);
    CHECK(h.handle(req("POST", "/haptic", "[1,2]")).status == 400);
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"secret"})")).status == 400);                 // no event
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"secret","event":"al ign"})")).status == 400);  // space
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"secret","event":""})")).status == 400);
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"secret","event":5})")).status == 400);
    CHECK(h.handle(req("POST", "/haptic", R"({"token":"secret","event":"align","intensity":"x"})")).status == 400);
    CHECK(h.handle(req("GET", "/haptic")).status == 404);
    CHECK(h.handle(req("GET", "/nope")).status == 404);
    CHECK(h.handle(req("DELETE", "/ping")).status == 404);
    CHECK(played.empty());
    for (auto path : {"/haptic", "/nope"}) {
        auto r = h.handle(req("POST", path, "{nope"));
        CHECK(nlohmann::json::parse(r.body).contains("error"));
    }
}

TEST_CASE("bridge: request parsing handles partial, complete and oversized input") {
    HttpRequest out;
    CHECK(ixpat::parseHttpRequest("GET /ping HTTP/1.1\r\nHost: x\r\n", out) == ParseState::incomplete);
    CHECK(ixpat::parseHttpRequest("GET /ping HTTP/1.1\r\nHost: x\r\n\r\n", out) == ParseState::complete);
    CHECK(out.method == "GET");
    CHECK(out.path == "/ping");
    CHECK(out.body.empty());
    std::string post = "POST /haptic HTTP/1.1\r\nContent-Type: application/json\r\ncontent-length: 5\r\n\r\n";
    CHECK(ixpat::parseHttpRequest(post + "{\"a\"", out) == ParseState::incomplete);
    CHECK(ixpat::parseHttpRequest(post + "{\"a\":", out) == ParseState::complete);
    CHECK(out.method == "POST");
    CHECK(out.body == "{\"a\":");
    CHECK(ixpat::parseHttpRequest(post + "{\"a\":}extra", out) == ParseState::complete);  // body is exactly 5
    CHECK(out.body == "{\"a\":");
    CHECK(ixpat::parseHttpRequest("POST /haptic HTTP/1.1\r\nContent-Length: 99999\r\n\r\n", out) == ParseState::bad);
    CHECK(ixpat::parseHttpRequest("POST /haptic HTTP/1.1\r\nContent-Length: -1\r\n\r\n", out) == ParseState::bad);
    CHECK(ixpat::parseHttpRequest("POST /haptic HTTP/1.1\r\nContent-Length: abc\r\n\r\n", out) == ParseState::bad);
    CHECK(ixpat::parseHttpRequest("GARBAGE\r\n\r\n", out) == ParseState::bad);
    CHECK(ixpat::parseHttpRequest("", out) == ParseState::incomplete);
    std::string hugeHeaders = "GET /ping HTTP/1.1\r\n" + std::string(17000, 'x');
    CHECK(ixpat::parseHttpRequest(hugeHeaders, out) == ParseState::bad);
    CHECK(ixpat::parseHttpRequest("OPTIONS /haptic HTTP/1.1\r\n\r\n", out) == ParseState::complete);
    CHECK(out.method == "OPTIONS");
}

TEST_CASE("bridge: OPTIONS preflight is answered for CORS") {
    std::vector<Played> played;
    auto r = handler(played).handle(req("OPTIONS", "/haptic"));
    CHECK(r.status == 204);
}

TEST_CASE("bridge: responses carry the JSON type, CORS headers, exact length and close") {
    std::string s = ixpat::serializeResponse({200, R"({"ok":true})"});
    CHECK(s.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(s.find("Content-Type: application/json\r\n") != std::string::npos);
    CHECK(s.find("Content-Length: 11\r\n") != std::string::npos);
    CHECK(s.find("Access-Control-Allow-Origin: *\r\n") != std::string::npos);
    CHECK(s.find("Access-Control-Allow-Headers: content-type\r\n") != std::string::npos);
    CHECK(s.find("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n") != std::string::npos);
    CHECK(s.find("Connection: close\r\n") != std::string::npos);
    CHECK(s.ends_with("\r\n\r\n{\"ok\":true}"));
    CHECK(ixpat::serializeResponse({404, "{}"}).starts_with("HTTP/1.1 404 Not Found\r\n"));
    CHECK(ixpat::serializeResponse({401, "{}"}).starts_with("HTTP/1.1 401 Unauthorized\r\n"));
    CHECK(ixpat::serializeResponse({400, "{}"}).starts_with("HTTP/1.1 400 Bad Request\r\n"));
    CHECK(ixpat::serializeResponse({204, ""}).starts_with("HTTP/1.1 204 No Content\r\n"));
}
