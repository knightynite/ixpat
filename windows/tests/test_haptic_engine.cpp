#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

#include "core/haptic_engine.h"

using ixpat::EngineParams;
using ixpat::HapticEngine;
using ixpat::HapticEvent;
using ixpat::HapticRule;

namespace {

struct Rig {
    HapticEngine e;
    std::vector<HapticEngine::Play> plays;
    std::vector<HapticEngine::Deferred> deferred;
    std::vector<std::string> labels;
    Rig() {
        e.trigger = [this](HapticEngine::Play p, std::string_view label) {
            plays.push_back(p);
            labels.emplace_back(label);
        };
        e.defer = [this](HapticEngine::Deferred d) { deferred.push_back(d); };
        e.setConnected(true);
    }
};

EngineParams params(int master = 4) {
    EngineParams p;
    p.enabled = true;
    p.effHapticsEnabled = true;
    p.masterIntensity = master;
    return p;
}

}  // namespace

TEST_CASE("engine: nothing plays while disconnected") {
    Rig r;
    r.e.setConnected(false);
    CHECK_FALSE(r.e.play(10, 3, "test", 1.0));
    CHECK(r.plays.empty());
    CHECK(r.e.pulseCount() == 0);
}

TEST_CASE("engine: triggers are spaced by the requested waveform's duration (35 ms floor), as on the Mac") {
    Rig r;
    CHECK(r.e.play(7, 3, "grow", 1.0));          // Grow: 200 ms
    CHECK_FALSE(r.e.play(7, 3, "grow", 1.1));    // another Grow 100 ms later: dropped
    CHECK(r.e.play(10, 3, "click", 1.15));       // a Click 150 ms later: its own window is the 35 ms floor, so it plays
    CHECK(r.e.play(10, 3, "click", 1.20));       // 50 ms later: fine
    CHECK_FALSE(r.e.play(10, 3, "click", 1.22));  // 20 ms later: inside the 35 ms floor
    CHECK(r.plays.size() == 3);
    CHECK(r.e.pulseCount() == 3);
    CHECK(r.e.lastPlayed().find("click") != std::string::npos);
    CHECK(r.e.lastPlayed().find("Click") != std::string::npos);
}

TEST_CASE("engine: unknown ordinals use a 50 ms duration") {
    Rig r;
    CHECK(r.e.play(42, 3, "x", 1.0));
    CHECK_FALSE(r.e.play(42, 3, "x", 1.04));
    CHECK(r.e.play(42, 3, "x", 1.051));
}

TEST_CASE("engine: a rule plays its ordinal scaled by the master strength") {
    Rig r;
    HapticRule rule{true, 10, 2, std::nullopt};
    r.e.play(HapticEvent::leftDown, rule, params(4), 1.0);
    REQUIRE(r.plays.size() == 1);
    CHECK(r.plays[0].ordinal == 10);
    CHECK(r.plays[0].intensity == 2);
    CHECK(r.e.lastEvent() == "Left click");
    r.e.play(HapticEvent::leftDown, rule, params(2), 2.0);  // 2 * 2 / 4 = 1
    CHECK(r.plays[1].intensity == 1);
    HapticRule strong{true, 10, 4, std::nullopt};
    r.e.play(HapticEvent::leftDown, strong, params(1), 3.0);  // 4 * 1 / 4 = 1
    CHECK(r.plays[2].intensity == 1);
    HapticRule light{true, 3, 1, std::nullopt};
    r.e.play(HapticEvent::leftUp, light, params(1), 4.0);  // 1 * 1 / 4 = 0.25 -> at least 1
    CHECK(r.plays[3].intensity == 1);
    HapticRule three{true, 6, 3, std::nullopt};
    r.e.play(HapticEvent::scrollStep, three, params(2), 5.0);  // 3 * 2 / 4 = 1.5 -> 2 (schoolbook rounding)
    CHECK(r.plays[4].intensity == 2);
}

TEST_CASE("engine: master off, disabled rule, zero intensity, or haptics off play nothing") {
    Rig r;
    HapticRule rule{true, 10, 2, std::nullopt};
    r.e.play(HapticEvent::leftDown, rule, params(0), 1.0);
    HapticRule off{false, 10, 2, std::nullopt};
    r.e.play(HapticEvent::leftDown, off, params(4), 2.0);
    HapticRule zero{true, 10, 0, std::nullopt};
    r.e.play(HapticEvent::leftDown, zero, params(4), 3.0);
    auto p = params(4);
    p.effHapticsEnabled = false;
    r.e.play(HapticEvent::leftDown, rule, p, 4.0);
    p.effHapticsEnabled = true;
    p.enabled = false;
    r.e.play(HapticEvent::leftDown, rule, p, 5.0);
    CHECK(r.plays.empty());
    CHECK(r.e.lastEvent() == "Left click");  // the disabled rule still records the event name, like the Mac
}

TEST_CASE("engine: boost shifts the rule's intensity within 1..4 before scaling; an ordinal override swaps the waveform") {
    Rig r;
    HapticRule rule{true, 6, 4, std::nullopt};
    r.e.play(HapticEvent::scrollStep, rule, params(4), 1.0, std::nullopt, 1);  // 5 -> 4
    CHECK(r.plays[0].intensity == 4);
    r.e.play(HapticEvent::scrollStep, rule, params(4), 2.0, 3, -1);  // 3, waveform Hover
    CHECK(r.plays[1].intensity == 3);
    CHECK(r.plays[1].ordinal == 3);
    HapticRule one{true, 6, 1, std::nullopt};
    r.e.play(HapticEvent::scrollStep, one, params(4), 3.0, std::nullopt, -1);  // 0 -> 1
    CHECK(r.plays[2].intensity == 1);
}

TEST_CASE("engine: an echo is deferred by at least 20 ms with its own scaled intensity") {
    Rig r;
    HapticRule rule{true, 10, 4, ixpat::HapticEcho{4, 1, 45}};
    r.e.play(HapticEvent::leftDown, rule, params(2), 1.0);
    REQUIRE(r.plays.size() == 1);
    CHECK(r.plays[0].intensity == 2);
    REQUIRE(r.deferred.size() == 1);
    CHECK(r.deferred[0].delayMs == 45);
    CHECK(r.deferred[0].play.ordinal == 4);
    CHECK(r.deferred[0].play.intensity == 1);  // max(1, round(1*2/4)) = 1
    CHECK(r.deferred[0].label == "Left click echo");
    HapticRule quick{true, 10, 2, ixpat::HapticEcho{10, 2, 5}};
    r.e.play(HapticEvent::leftDown, quick, params(4), 2.0);
    CHECK(r.deferred[1].delayMs == 20);
    HapticRule silentEcho{true, 10, 2, ixpat::HapticEcho{10, 0, 50}};
    r.e.play(HapticEvent::leftDown, silentEcho, params(4), 3.0);
    CHECK(r.deferred.size() == 2);  // echo intensity 0: none
}

TEST_CASE("engine: preview plays the rule as the user would feel it, unscaled") {
    Rig r;
    HapticRule rule{true, 4, 3, ixpat::HapticEcho{5, 3, 55}};
    r.e.preview(rule, "Push back", 1.0);
    REQUIRE(r.plays.size() == 1);
    CHECK(r.plays[0].ordinal == 4);
    CHECK(r.plays[0].intensity == 3);
    REQUIRE(r.deferred.size() == 1);
    CHECK(r.deferred[0].play.intensity == 3);
    CHECK(r.deferred[0].label == "Push back echo");
}

TEST_CASE("engine: the device table drives durations and names") {
    Rig r;
    std::vector<ixpat::Waveform> table{{3, 0x1008, 500}};
    r.e.setTable(table);
    CHECK(r.e.play(3, 1, "hover", 1.0));
    CHECK_FALSE(r.e.play(3, 1, "hover", 1.4));
    CHECK(r.e.play(3, 1, "hover", 1.51));
}
