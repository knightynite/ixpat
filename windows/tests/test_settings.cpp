#include <doctest/doctest.h>

#include <limits>
#include <ostream>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "core/settings.h"
#include "fixture.h"

using ixpat::ActionKind;
using ixpat::ButtonGesture;
using ixpat::HapticEvent;
using ixpat::Settings;

static Settings parseOk(const std::string& text) {
    auto r = ixpat::parseSettings(text);
    if (auto* e = std::get_if<ixpat::ParseError>(&r)) FAIL("parse error: " << e->message);
    return std::get<Settings>(r);
}

TEST_CASE("settings: defaults match Settings.swift") {
    auto s = ixpat::defaultSettings();
    CHECK(s.enabled);
    CHECK(s.hapticsEnabled);
    CHECK(s.masterIntensity == 4);
    CHECK(s.scrollStepEvery == 1);
    CHECK_FALSE(s.managePointer);
    CHECK(s.pointerSpeed == 3.0);
    CHECK(s.scrollSpeed == 0.3125);
    CHECK(s.smoothScroll);
    CHECK(s.scrollPixelsPerNotch == 60);
    CHECK(s.scrollGlide == 5);
    CHECK(s.scrollMomentum == 4);
    CHECK(s.scrollAccel == 1);
    CHECK(s.freeSpin);
    CHECK(s.ratchetDynamic);
    CHECK(s.ratchetLightRate == 10);
    CHECK(s.freeSpinThreshold == 18);
    CHECK(s.freeSpinMultiplier == 3);
    CHECK(s.ringItems.size() == 8);
    CHECK(s.action(ButtonGesture::press).kind == ActionKind::actionRing);
    CHECK(s.action(ButtonGesture::doublePress).kind == ActionKind::missionControl);
    CHECK(s.action(ButtonGesture::hold).kind == ActionKind::screenshot);
    CHECK(s.gestureUp.kind == ActionKind::volumeUp);
    CHECK(s.gestureDown.kind == ActionKind::volumeDown);
    CHECK(s.gestureLeft.kind == ActionKind::previousTrack);
    CHECK(s.gestureRight.kind == ActionKind::nextTrack);
    CHECK(s.gestureStepPx == 24);
    CHECK(s.gestureSwipePx == 90);
    CHECK(s.gestureFreezePointer);
    CHECK(s.gesturesOnHold);
    CHECK(s.profiles.empty());
    CHECK(s.bridgeEnabled);
    CHECK(s.bridgePort == 47337);
    CHECK(s.bridgeToken.size() == 24);
    CHECK(s.autoCheckUpdates);
    CHECK(s.clickHoldMs == 300);
    CHECK(s.forceClick);
    CHECK(s.forceClickMs == 350);
    CHECK(s.freeSpinTextureEvery == 2);
    CHECK(s.swallowButton);
    CHECK(s.theme == ixpat::AppTheme::system);
    CHECK(s.accentHex == 0x0A84FFu);
    CHECK(s.windows.holdAction == ixpat::HoldAction::contextMenu);
    CHECK_FALSE(s.windows.startAtLogin);
    CHECK(s.baseRule(HapticEvent::leftDown) == ixpat::defaultRule(HapticEvent::leftDown));
    CHECK(s.baseRule(HapticEvent::forceClick) == ixpat::defaultRule(HapticEvent::forceClick));
}

TEST_CASE("settings: Mac-written file loads, round-trips, and keeps a profile") {
    auto s = parseOk(readFixture("settings_mac_defaults.json"));
    REQUIRE(s.profiles.size() == 1);
    CHECK(s.profiles[0].bundleID == "com.google.Chrome");
    CHECK(s.profiles[0].id == "3B9E6B0A-6C8E-4B5B-9E3A-0F0F0F0F0F0F");
    CHECK(s.profiles[0].smoothScroll == false);
    CHECK(s.bridgeToken == "k3j4h5g6f7d8s9a0q1w2e3r4");
    CHECK(s.ringItems.size() == 8);
    CHECK(s.ringItems[6].action.kind == ActionKind::shortcut);
    CHECK(s.ringItems[6].action.flags == 1048576u);
    CHECK(s.action(ButtonGesture::hold).kind == ActionKind::screenshot);
    CHECK(s.baseRule(HapticEvent::leftDown).ordinal == 10);
    CHECK(s.theme == ixpat::AppTheme::system);
    auto text = ixpat::toJson(s);
    auto again = parseOk(text);
    CHECK(ixpat::toJson(again) == text);
    auto j = nlohmann::json::parse(text);
    CHECK(j["platform"] == "windows");
    CHECK(j["windows"]["holdAction"] == "contextMenu");
    CHECK(j["rules"].size() == ixpat::kHapticEventCount);
    CHECK(j["actions"]["press"]["actionRing"].is_object());
    // Keys come out sorted, like Swift's .sortedKeys.
    std::string prev;
    for (auto& [k, v] : j.items()) {
        CHECK(prev < k);
        prev = k;
    }
}

TEST_CASE("settings: older RawMap array layout for rules and actions loads") {
    auto s = parseOk(readFixture("settings_rawmap_array.json"));
    CHECK(s.masterIntensity == 3);
    CHECK(s.baseRule(HapticEvent::leftDown).enabled == true);
    CHECK(s.baseRule(HapticEvent::leftUp) == ixpat::HapticRule{true, 3, 1, std::nullopt});
    CHECK(s.baseRule(HapticEvent::rightDown) == ixpat::defaultRule(HapticEvent::rightDown));  // untouched
    CHECK(s.action(ButtonGesture::press).kind == ActionKind::actionRing);
    CHECK(s.action(ButtonGesture::hold).kind == ActionKind::lockScreen);
    CHECK(s.action(ButtonGesture::doublePress).kind == ActionKind::none);  // absent in the file
    CHECK(s.theme == ixpat::AppTheme::dark);
}

TEST_CASE("settings: hand-edited nonsense is clamped") {
    auto s = parseOk(R"({"masterIntensity":99,"scrollGlide":1e300,"forceClickMs":-5,"bridgePort":80,
        "scrollPixelsPerNotch":"abc","gestureStepPx":null,
        "rules":{"leftDown":{"enabled":true,"ordinal":999,"intensity":0,"echo":{"ordinal":-1,"intensity":9,"delayMs":99999}}}})");
    CHECK(s.masterIntensity == 4);
    CHECK(s.scrollGlide == 10);
    CHECK(s.forceClickMs == 200);
    CHECK(s.bridgePort == 1024);
    CHECK(s.scrollPixelsPerNotch == 60);  // wrong type: default
    CHECK(s.gestureStepPx == 24);         // null: default
    auto& r = s.baseRule(HapticEvent::leftDown);
    CHECK(r.ordinal == 255);
    CHECK(r.intensity == 1);
    REQUIRE(r.echo.has_value());
    CHECK(r.echo->ordinal == 0);
    CHECK(r.echo->intensity == 4);
    CHECK(r.echo->delayMs == 2000);
}

TEST_CASE("settings: out-of-range and huge numbers clamp like Swift instead of wrapping") {
    CHECK(parseOk(R"({"masterIntensity":2147483648})").masterIntensity == 4);
    CHECK(parseOk(R"({"masterIntensity":1e300})").masterIntensity == 4);
    CHECK(parseOk(R"({"masterIntensity":-1e300})").masterIntensity == 0);
    CHECK(parseOk(R"({"clickHoldMs":1e19})").clickHoldMs == 1500);
    CHECK(parseOk(R"({"bridgePort":-9999999999})").bridgePort == 1024);
    CHECK(parseOk(R"({"accentHex":-5})").accentHex == 0x0A84FFu);        // negative for an unsigned: default
    CHECK(parseOk(R"({"accentHex":4294967296})").accentHex == 0x0A84FFu);  // too big: default
    CHECK(parseOk(R"({"accentHex":16711680})").accentHex == 0xFF0000u);
    CHECK(parseOk(R"({"masterIntensity":2.7})").masterIntensity == 2);    // a float for an int: truncated
}

TEST_CASE("settings: NaN and infinity become defaults") {
    auto s = ixpat::defaultSettings();
    s.scrollGlide = std::numeric_limits<double>::quiet_NaN();
    s.pointerSpeed = -std::numeric_limits<double>::infinity();
    ixpat::sanitize(s);
    CHECK(s.scrollGlide == 5);
    CHECK(s.pointerSpeed == 3);  // not finite: the default, as Settings.swift's clamp does
}

TEST_CASE("settings: malformed JSON is an error, unknown keys and bad rule names are ignored") {
    CHECK(std::holds_alternative<ixpat::ParseError>(ixpat::parseSettings("{nope")));
    CHECK(std::holds_alternative<ixpat::ParseError>(ixpat::parseSettings("[1,2]")));
    auto s = parseOk(R"({"enabled":false,"future":[1,2,3],"rules":{"teleport":{"enabled":true,"ordinal":1,"intensity":1}}})");
    CHECK_FALSE(s.enabled);
    CHECK(s.bridgeToken.size() == 24);  // generated when the file has none
    CHECK(s.baseRule(HapticEvent::leftDown) == ixpat::defaultRule(HapticEvent::leftDown));
}

TEST_CASE("settings: a bad action or ring item in the file does not lose the rest") {
    auto s = parseOk(R"({"actions":{"press":{"teleport":{}},"hold":{"lockScreen":{}}},
        "ringItems":[{"id":"X","title":"ok","symbol":"","action":{"spotlight":{}}},{"title":"bad","action":{"nope":{}}}]})");
    CHECK(s.action(ButtonGesture::press).kind == ActionKind::none);
    CHECK(s.action(ButtonGesture::hold).kind == ActionKind::lockScreen);
    REQUIRE(s.ringItems.size() == 1);
    CHECK(s.ringItems[0].title == "ok");
}

TEST_CASE("settings: profile overrides resolve through eff*") {
    auto s = ixpat::defaultSettings();
    ixpat::AppProfile p;
    p.id = ixpat::newUuidUpper();
    p.bundleID = "chrome.exe";
    p.smoothScroll = false;
    p.ratchetLightRate = 20;
    p.rules["leftDown"] = ixpat::HapticRule{true, 3, 1, std::nullopt};
    s.profiles.push_back(p);
    CHECK(s.profile() == nullptr);
    s.frontBundleID = "chrome.exe";
    REQUIRE(s.profile() != nullptr);
    CHECK_FALSE(s.effSmoothScroll());
    CHECK(s.effRatchetLightRate() == 20);
    CHECK(s.effFreeSpin());  // not overridden
    CHECK(s.effRingItems().size() == 8);
    CHECK(s.rule(HapticEvent::leftDown).ordinal == 3);
    CHECK(s.baseRule(HapticEvent::leftDown).ordinal == 10);
    CHECK(s.effGestureUp().kind == ActionKind::volumeUp);
    s.frontBundleID = "other.exe";
    CHECK(s.effSmoothScroll());
    CHECK(s.rule(HapticEvent::leftDown).ordinal == 10);
}

TEST_CASE("settings: windows section round-trips") {
    auto s = ixpat::defaultSettings();
    s.windows.holdAction = ixpat::HoldAction::middleClick;
    s.windows.startAtLogin = true;
    auto back = parseOk(ixpat::toJson(s));
    CHECK(back.windows.holdAction == ixpat::HoldAction::middleClick);
    CHECK(back.windows.startAtLogin);
    auto odd = parseOk(R"({"windows":{"holdAction":"teleport"}})");
    CHECK(odd.windows.holdAction == ixpat::HoldAction::contextMenu);
}

TEST_CASE("settings: bridge token is 24 lowercase alphanumerics and random") {
    auto a = ixpat::newBridgeToken(), b = ixpat::newBridgeToken();
    CHECK(a.size() == 24);
    CHECK(a != b);
    for (char c : a) CHECK(((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')));
}
