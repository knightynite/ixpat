#include "core/json_compat.h"

#include <algorithm>
#include <cmath>

namespace ixpat {
namespace {

using json = nlohmann::json;

template <class T>
void putOptional(json& j, const char* key, const std::optional<T>& v) {
    if (v) j[key] = *v;
}
template <class T>
void getOptional(const json& j, const char* key, std::optional<T>& v) {
    if (j.contains(key) && !j[key].is_null()) v = j[key].get<T>();
}

[[noreturn]] void fail(const json& j, const char* what) {
    throw json::other_error::create(501, what, &j);
}

/// A required integer, clamped into int range before narrowing (a 2^31 in the file must not wrap).
int requiredInt(const json& j, const char* key) {
    const json& v = j.at(key);
    if (!v.is_number()) fail(j, "expected a number");
    const double d = v.get<double>();
    if (!std::isfinite(d)) fail(j, "expected a finite number");
    return static_cast<int>(std::clamp(d, -2147483648.0, 2147483647.0));
}
int optionalInt(const json& j, const char* key, int def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    const double d = it->get<double>();
    if (!std::isfinite(d)) return def;
    return static_cast<int>(std::clamp(d, -2147483648.0, 2147483647.0));
}

}  // namespace

void to_json(json& j, const HapticEcho& e) {
    j = json{{"ordinal", e.ordinal}, {"intensity", e.intensity}, {"delayMs", e.delayMs}};
}
void from_json(const json& j, HapticEcho& e) {
    e.ordinal = requiredInt(j, "ordinal");
    e.intensity = requiredInt(j, "intensity");
    e.delayMs = requiredInt(j, "delayMs");
}

void to_json(json& j, const HapticRule& r) {
    j = json{{"enabled", r.enabled}, {"ordinal", r.ordinal}, {"intensity", r.intensity}};
    putOptional(j, "echo", r.echo);
}
void from_json(const json& j, HapticRule& r) {
    j.at("enabled").get_to(r.enabled);
    r.ordinal = requiredInt(j, "ordinal");
    r.intensity = requiredInt(j, "intensity");
    r.echo.reset();
    getOptional(j, "echo", r.echo);
}

void to_json(json& j, const ButtonAction& a) {
    json payload = json::object();
    switch (a.kind) {
        case ActionKind::launchApp:
            payload["path"] = a.path;
            break;
        case ActionKind::shortcut:
            payload["keyCode"] = a.keyCode;
            payload["flags"] = a.flags;
            payload["label"] = a.label;
            break;
        case ActionKind::shell:
            payload["command"] = a.command;
            break;
        default:
            break;
    }
    j = json::object();
    j[std::string(rawName(a.kind))] = std::move(payload);
}
void from_json(const json& j, ButtonAction& a) {
    if (!j.is_object() || j.size() != 1) fail(j, "ButtonAction: expected an object with exactly one key");
    const auto it = j.begin();
    const std::string& key = it.key();
    const json& payload = it.value();
    auto kind = actionKindFromRaw(key);
    if (!kind) fail(j, "ButtonAction: unknown case");
    a = ButtonAction::blank(*kind);
    if (!payload.is_object()) fail(j, "ButtonAction: payload must be an object");
    switch (*kind) {
        case ActionKind::launchApp:
            a.path = payload.value("path", std::string{});
            break;
        case ActionKind::shortcut:
            a.keyCode = optionalInt(payload, "keyCode", -1);
            if (auto f = payload.find("flags"); f != payload.end() && f->is_number_unsigned()) a.flags = f->get<std::uint64_t>();
            else if (f != payload.end() && f->is_number_integer() && f->get<std::int64_t>() >= 0) a.flags = static_cast<std::uint64_t>(f->get<std::int64_t>());
            a.label = payload.value("label", std::string{});
            break;
        case ActionKind::shell:
            a.command = payload.value("command", std::string{});
            break;
        default:
            break;
    }
}

void to_json(json& j, const RingItem& r) {
    j = json{{"id", r.id}, {"title", r.title}, {"symbol", r.symbol}, {"action", r.action}};
}
void from_json(const json& j, RingItem& r) {
    r.id = j.value("id", std::string{});
    if (r.id.empty()) r.id = newUuidUpper();
    j.at("title").get_to(r.title);
    r.symbol = j.value("symbol", std::string{});
    j.at("action").get_to(r.action);
}

void to_json(json& j, const AppProfile& p) {
    j = json{{"id", p.id}, {"bundleID", p.bundleID}, {"name", p.name}, {"rules", p.rules}};
    putOptional(j, "hapticsEnabled", p.hapticsEnabled);
    putOptional(j, "smoothScroll", p.smoothScroll);
    putOptional(j, "freeSpin", p.freeSpin);
    putOptional(j, "ratchetDynamic", p.ratchetDynamic);
    putOptional(j, "ratchetLightRate", p.ratchetLightRate);
    putOptional(j, "scrollStepEvery", p.scrollStepEvery);
    putOptional(j, "freeSpinTextureEvery", p.freeSpinTextureEvery);
    putOptional(j, "ringItems", p.ringItems);
    putOptional(j, "gestureUp", p.gestureUp);
    putOptional(j, "gestureDown", p.gestureDown);
    putOptional(j, "gestureLeft", p.gestureLeft);
    putOptional(j, "gestureRight", p.gestureRight);
}
void from_json(const json& j, AppProfile& p) {
    p = AppProfile{};
    p.id = j.value("id", std::string{});
    if (p.id.empty()) p.id = newUuidUpper();
    j.at("bundleID").get_to(p.bundleID);
    p.name = j.value("name", std::string{});
    if (j.contains("rules")) j["rules"].get_to(p.rules);
    getOptional(j, "hapticsEnabled", p.hapticsEnabled);
    getOptional(j, "smoothScroll", p.smoothScroll);
    getOptional(j, "freeSpin", p.freeSpin);
    getOptional(j, "ratchetDynamic", p.ratchetDynamic);
    getOptional(j, "ratchetLightRate", p.ratchetLightRate);
    getOptional(j, "scrollStepEvery", p.scrollStepEvery);
    getOptional(j, "freeSpinTextureEvery", p.freeSpinTextureEvery);
    getOptional(j, "ringItems", p.ringItems);
    getOptional(j, "gestureUp", p.gestureUp);
    getOptional(j, "gestureDown", p.gestureDown);
    getOptional(j, "gestureLeft", p.gestureLeft);
    getOptional(j, "gestureRight", p.gestureRight);
}

}  // namespace ixpat
