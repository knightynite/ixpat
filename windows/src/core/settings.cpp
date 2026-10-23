#include "core/settings.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/json_compat.h"

namespace ixpat {
namespace {

using json = nlohmann::json;

constexpr std::array<std::string_view, 3> kThemeRaw{"light", "dark", "system"};
constexpr std::array<std::string_view, 4> kHoldRaw{"none", "contextMenu", "middleClick", "actionRing"};

/// A value from the file if it is there and has the right type; otherwise `def`.
template <class T>
T getOr(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return def;
    }
}

/// Swift's `RawMap`: a string-keyed object, or the older alternating array `[key, value, key, value]`.
template <class F>
void forEachRawMapEntry(const json& j, F&& f) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) f(it.key(), it.value());
    } else if (j.is_array()) {
        for (std::size_t i = 0; i + 1 < j.size(); i += 2) {
            if (j[i].is_string()) f(j[i].get<std::string>(), j[i + 1]);
        }
    }
}

template <class T>
std::vector<T> readList(const json& j, const char* key, std::vector<T> def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return def;
    std::vector<T> out;
    for (const auto& el : *it) {
        try {
            out.push_back(el.get<T>());
        } catch (const json::exception&) {
            // one bad entry must not lose the rest
        }
    }
    return out;
}

void clampD(double& v, double lo, double hi, double dflt) { v = std::isfinite(v) ? std::clamp(v, lo, hi) : dflt; }
void clampI(int& v, int lo, int hi) { v = std::clamp(v, lo, hi); }

HapticRule clampRule(const HapticRule& r) {
    HapticRule out{r.enabled, std::clamp(r.ordinal, 0, 255), std::clamp(r.intensity, 1, 4), std::nullopt};
    if (r.echo) {
        out.echo = HapticEcho{std::clamp(r.echo->ordinal, 0, 255), std::clamp(r.echo->intensity, 1, 4),
                              std::clamp(r.echo->delayMs, 0, 2000)};
    }
    return out;
}

}  // namespace

std::string_view rawName(AppTheme t) { return kThemeRaw[static_cast<std::size_t>(t)]; }
std::optional<AppTheme> themeFromRaw(std::string_view raw) {
    for (std::size_t i = 0; i < kThemeRaw.size(); ++i) {
        if (kThemeRaw[i] == raw) return static_cast<AppTheme>(i);
    }
    return std::nullopt;
}
std::string_view rawName(HoldAction a) { return kHoldRaw[static_cast<std::size_t>(a)]; }
std::optional<HoldAction> holdActionFromRaw(std::string_view raw) {
    for (std::size_t i = 0; i < kHoldRaw.size(); ++i) {
        if (kHoldRaw[i] == raw) return static_cast<HoldAction>(i);
    }
    return std::nullopt;
}

// MARK: effective values

const AppProfile* Settings::profile() const {
    if (!frontBundleID) return nullptr;
    for (const auto& p : profiles) {
        if (p.bundleID == *frontBundleID) return &p;
    }
    return nullptr;
}
const HapticRule& Settings::rule(HapticEvent e) const {
    if (const auto* p = profile()) {
        auto it = p->rules.find(std::string(rawName(e)));
        if (it != p->rules.end()) return it->second;
    }
    return baseRule(e);
}
bool Settings::effHapticsEnabled() const { const auto* p = profile(); return p && p->hapticsEnabled ? *p->hapticsEnabled : hapticsEnabled; }
bool Settings::effSmoothScroll() const { const auto* p = profile(); return p && p->smoothScroll ? *p->smoothScroll : smoothScroll; }
bool Settings::effFreeSpin() const { const auto* p = profile(); return p && p->freeSpin ? *p->freeSpin : freeSpin; }
bool Settings::effRatchetDynamic() const { const auto* p = profile(); return p && p->ratchetDynamic ? *p->ratchetDynamic : ratchetDynamic; }
double Settings::effRatchetLightRate() const { const auto* p = profile(); return p && p->ratchetLightRate ? *p->ratchetLightRate : ratchetLightRate; }
int Settings::effScrollStepEvery() const { const auto* p = profile(); return p && p->scrollStepEvery ? *p->scrollStepEvery : scrollStepEvery; }
int Settings::effFreeSpinTextureEvery() const { const auto* p = profile(); return p && p->freeSpinTextureEvery ? *p->freeSpinTextureEvery : freeSpinTextureEvery; }
const std::vector<RingItem>& Settings::effRingItems() const { const auto* p = profile(); return p && p->ringItems ? *p->ringItems : ringItems; }
const ButtonAction& Settings::effGestureUp() const { const auto* p = profile(); return p && p->gestureUp ? *p->gestureUp : gestureUp; }
const ButtonAction& Settings::effGestureDown() const { const auto* p = profile(); return p && p->gestureDown ? *p->gestureDown : gestureDown; }
const ButtonAction& Settings::effGestureLeft() const { const auto* p = profile(); return p && p->gestureLeft ? *p->gestureLeft : gestureLeft; }
const ButtonAction& Settings::effGestureRight() const { const auto* p = profile(); return p && p->gestureRight ? *p->gestureRight : gestureRight; }

// MARK: defaults, token, sanitize

Settings defaultSettings() {
    Settings s;
    for (std::size_t i = 0; i < kHapticEventCount; ++i) s.rules[i] = defaultRule(static_cast<HapticEvent>(i));
    s.ringItems = defaultRingItems();
    s.action(ButtonGesture::press) = ButtonAction::blank(ActionKind::actionRing);
    s.action(ButtonGesture::doublePress) = ButtonAction::blank(ActionKind::missionControl);
    s.action(ButtonGesture::hold) = ButtonAction::blank(ActionKind::screenshot);
    s.gestureUp = ButtonAction::blank(ActionKind::volumeUp);
    s.gestureDown = ButtonAction::blank(ActionKind::volumeDown);
    s.gestureLeft = ButtonAction::blank(ActionKind::previousTrack);
    s.gestureRight = ButtonAction::blank(ActionKind::nextTrack);
    s.bridgeToken = newBridgeToken();
    return s;
}

std::string newBridgeToken() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    constexpr std::string_view chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<std::size_t> pick(0, chars.size() - 1);
    std::string t(24, ' ');
    for (auto& c : t) c = chars[pick(rng)];
    return t;
}

void sanitize(Settings& s) {
    clampI(s.masterIntensity, 0, 4);
    clampI(s.scrollStepEvery, 1, 10);
    clampD(s.pointerSpeed, 0, 3, 3);
    clampD(s.scrollSpeed, 0.05, 1.5, 0.3125);
    clampD(s.scrollPixelsPerNotch, 10, 400, 60);
    clampD(s.scrollGlide, 1, 10, 5);
    clampD(s.scrollMomentum, 0, 10, 4);
    clampD(s.scrollAccel, 0, 3, 1);
    clampD(s.freeSpinThreshold, 5, 60, 18);
    clampD(s.freeSpinMultiplier, 1, 10, 3);
    clampD(s.ratchetLightRate, 2, 40, 10);
    clampD(s.gestureStepPx, 4, 200, 24);
    clampD(s.gestureSwipePx, 20, 600, 90);
    clampI(s.clickHoldMs, 150, 1500);
    clampI(s.freeSpinTextureEvery, 1, 6);
    clampI(s.forceClickMs, 200, 800);
    clampI(s.bridgePort, 1024, 65535);
    for (auto& r : s.rules) r = clampRule(r);
    for (auto& p : s.profiles) {
        for (auto& [k, r] : p.rules) r = clampRule(r);
        if (p.ratchetLightRate) clampD(*p.ratchetLightRate, 2, 40, 10);
        if (p.scrollStepEvery) clampI(*p.scrollStepEvery, 1, 10);
        if (p.freeSpinTextureEvery) clampI(*p.freeSpinTextureEvery, 1, 6);
    }
}

// MARK: JSON

std::string toJson(const Settings& s) {
    json rules = json::object();
    for (std::size_t i = 0; i < kHapticEventCount; ++i) {
        rules[std::string(rawName(static_cast<HapticEvent>(i)))] = s.rules[i];
    }
    json actions = json::object();
    for (std::size_t i = 0; i < s.actions.size(); ++i) {
        actions[std::string(rawName(static_cast<ButtonGesture>(i)))] = s.actions[i];
    }
    json j = {
        {"platform", "windows"},
        {"enabled", s.enabled},
        {"hapticsEnabled", s.hapticsEnabled},
        {"masterIntensity", s.masterIntensity},
        {"rules", std::move(rules)},
        {"scrollStepEvery", s.scrollStepEvery},
        {"managePointer", s.managePointer},
        {"pointerSpeed", s.pointerSpeed},
        {"noAcceleration", s.noAcceleration},
        {"scrollSpeed", s.scrollSpeed},
        {"reverseScroll", s.reverseScroll},
        {"smoothScroll", s.smoothScroll},
        {"scrollPixelsPerNotch", s.scrollPixelsPerNotch},
        {"scrollGlide", s.scrollGlide},
        {"scrollMomentum", s.scrollMomentum},
        {"scrollAccel", s.scrollAccel},
        {"freeSpin", s.freeSpin},
        {"freeSpinThreshold", s.freeSpinThreshold},
        {"freeSpinMultiplier", s.freeSpinMultiplier},
        {"ratchetDynamic", s.ratchetDynamic},
        {"ratchetLightRate", s.ratchetLightRate},
        {"ringItems", s.ringItems},
        {"actions", std::move(actions)},
        {"swallowButton", s.swallowButton},
        {"theme", std::string(rawName(s.theme))},
        {"accentHex", s.accentHex},
        {"gestureUp", s.gestureUp},
        {"gestureDown", s.gestureDown},
        {"gestureLeft", s.gestureLeft},
        {"gestureRight", s.gestureRight},
        {"gestureStepPx", s.gestureStepPx},
        {"gestureSwipePx", s.gestureSwipePx},
        {"gestureFreezePointer", s.gestureFreezePointer},
        {"gesturesOnHold", s.gesturesOnHold},
        {"clickHoldMs", s.clickHoldMs},
        {"freeSpinTextureEvery", s.freeSpinTextureEvery},
        {"forceClick", s.forceClick},
        {"forceClickMs", s.forceClickMs},
        {"profiles", s.profiles},
        {"bridgeEnabled", s.bridgeEnabled},
        {"bridgePort", s.bridgePort},
        {"bridgeToken", s.bridgeToken},
        {"autoCheckUpdates", s.autoCheckUpdates},
        {"windows", {{"holdAction", std::string(rawName(s.windows.holdAction))}, {"startAtLogin", s.windows.startAtLogin}}},
    };
    return j.dump(2) + "\n";
}

std::variant<Settings, ParseError> parseSettings(std::string_view text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& e) {
        return ParseError{e.what()};
    }
    if (!j.is_object()) return ParseError{"settings.json is not a JSON object"};

    Settings s = defaultSettings();
    s.enabled = getOr(j, "enabled", s.enabled);
    s.hapticsEnabled = getOr(j, "hapticsEnabled", s.hapticsEnabled);
    s.masterIntensity = getOr(j, "masterIntensity", s.masterIntensity);
    if (auto it = j.find("rules"); it != j.end()) {
        forEachRawMapEntry(*it, [&](const std::string& key, const json& v) {
            auto e = hapticEventFromRaw(key);
            if (!e) return;  // unknown keys are dropped
            try {
                s.baseRule(*e) = v.get<HapticRule>();
            } catch (const json::exception&) {
            }
        });
    }
    s.scrollStepEvery = getOr(j, "scrollStepEvery", s.scrollStepEvery);
    s.managePointer = getOr(j, "managePointer", s.managePointer);
    s.pointerSpeed = getOr(j, "pointerSpeed", s.pointerSpeed);
    s.noAcceleration = getOr(j, "noAcceleration", s.noAcceleration);
    s.scrollSpeed = getOr(j, "scrollSpeed", s.scrollSpeed);
    s.reverseScroll = getOr(j, "reverseScroll", s.reverseScroll);
    s.smoothScroll = getOr(j, "smoothScroll", s.smoothScroll);
    s.scrollPixelsPerNotch = getOr(j, "scrollPixelsPerNotch", s.scrollPixelsPerNotch);
    s.scrollGlide = getOr(j, "scrollGlide", s.scrollGlide);
    s.scrollMomentum = getOr(j, "scrollMomentum", s.scrollMomentum);
    s.scrollAccel = getOr(j, "scrollAccel", s.scrollAccel);
    s.freeSpin = getOr(j, "freeSpin", s.freeSpin);
    s.freeSpinThreshold = getOr(j, "freeSpinThreshold", s.freeSpinThreshold);
    s.freeSpinMultiplier = getOr(j, "freeSpinMultiplier", s.freeSpinMultiplier);
    s.ratchetDynamic = getOr(j, "ratchetDynamic", s.ratchetDynamic);
    s.ratchetLightRate = getOr(j, "ratchetLightRate", s.ratchetLightRate);
    s.ringItems = readList<RingItem>(j, "ringItems", std::move(s.ringItems));
    if (auto it = j.find("actions"); it != j.end() && (it->is_object() || it->is_array())) {
        // The file's map replaces the whole dictionary, as in Swift: a gesture it omits does nothing.
        for (auto& a : s.actions) a = ButtonAction{};
        forEachRawMapEntry(*it, [&](const std::string& key, const json& v) {
            auto g = gestureFromRaw(key);
            if (!g) return;
            try {
                s.action(*g) = v.get<ButtonAction>();
            } catch (const json::exception&) {
                s.action(*g) = ButtonAction{};
            }
        });
    }
    s.swallowButton = getOr(j, "swallowButton", s.swallowButton);
    s.theme = themeFromRaw(getOr(j, "theme", std::string(rawName(s.theme)))).value_or(AppTheme::system);
    s.accentHex = getOr(j, "accentHex", s.accentHex);
    s.gestureUp = getOr(j, "gestureUp", s.gestureUp);
    s.gestureDown = getOr(j, "gestureDown", s.gestureDown);
    s.gestureLeft = getOr(j, "gestureLeft", s.gestureLeft);
    s.gestureRight = getOr(j, "gestureRight", s.gestureRight);
    s.gestureStepPx = getOr(j, "gestureStepPx", s.gestureStepPx);
    s.gestureSwipePx = getOr(j, "gestureSwipePx", s.gestureSwipePx);
    s.gestureFreezePointer = getOr(j, "gestureFreezePointer", s.gestureFreezePointer);
    s.gesturesOnHold = getOr(j, "gesturesOnHold", s.gesturesOnHold);
    s.clickHoldMs = getOr(j, "clickHoldMs", s.clickHoldMs);
    s.freeSpinTextureEvery = getOr(j, "freeSpinTextureEvery", s.freeSpinTextureEvery);
    s.forceClick = getOr(j, "forceClick", s.forceClick);
    s.forceClickMs = getOr(j, "forceClickMs", s.forceClickMs);
    s.profiles = readList<AppProfile>(j, "profiles", {});
    s.bridgeEnabled = getOr(j, "bridgeEnabled", s.bridgeEnabled);
    s.bridgePort = getOr(j, "bridgePort", s.bridgePort);
    s.bridgeToken = getOr(j, "bridgeToken", std::string{});
    if (s.bridgeToken.empty()) s.bridgeToken = newBridgeToken();
    s.autoCheckUpdates = getOr(j, "autoCheckUpdates", s.autoCheckUpdates);
    if (auto it = j.find("windows"); it != j.end() && it->is_object()) {
        s.windows.holdAction =
            holdActionFromRaw(getOr(*it, "holdAction", std::string(rawName(s.windows.holdAction))))
                .value_or(HoldAction::contextMenu);
        s.windows.startAtLogin = getOr(*it, "startAtLogin", s.windows.startAtLogin);
    }
    sanitize(s);
    return s;
}

}  // namespace ixpat
