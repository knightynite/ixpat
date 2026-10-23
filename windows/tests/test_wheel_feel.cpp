#include <doctest/doctest.h>

#include <ostream>
#include <vector>

#include "core/wheel_feel.h"

using ixpat::HapticEvent;
using ixpat::WheelFeelEngine;
using ixpat::WheelFeelParams;
using ixpat::WheelPulse;

namespace {

WheelFeelParams P() {
    WheelFeelParams p;  // defaults: enabled, freeSpin, dynamic, threshold 18, lightRate 10, every 1, texture every 2
    p.scrollStepOrdinal = 6;
    p.collideOrdinal = 4;
    p.stepOrdinal = 6;
    p.hoverOrdinal = 3;
    return p;
}

bool isFirm(const WheelPulse& w, int boost) {
    return w.event == HapticEvent::scrollStep && !w.ordinal.has_value() && w.boost == boost;
}

}  // namespace

TEST_CASE("wheel: slow notches are firm detents; the first after a pause lands harder") {
    WheelFeelEngine e;
    auto p = P();
    auto a = e.notch(1, 10.0, p);
    REQUIRE(a.size() == 1);
    CHECK(isFirm(a[0], 1));  // nothing before it: a pause
    auto b = e.notch(1, 10.3, p);
    REQUIRE(b.size() == 1);
    CHECK(isFirm(b[0], 0));
    auto c = e.notch(-1, 11.6, p);  // 1.3 s later: a pause, and too late to count as a reversal
    REQUIRE(c.size() == 1);
    CHECK(isFirm(c[0], 1));
    CHECK_FALSE(e.freeSpinActive());
}

TEST_CASE("wheel: a fast spin gives light ticks (Hover when the rule is Step), no closer than 40 ms") {
    WheelFeelEngine e;
    auto p = P();
    p.freeSpin = false;  // keep the ratchet engaged however fast
    std::vector<std::pair<double, WheelPulse>> light;
    double t = 5.0;
    for (int i = 0; i < 20; ++i, t += 0.0333) {
        for (auto& w : e.notch(1, t, p)) {
            if (w.boost == -1) light.emplace_back(t, w);
        }
    }
    CHECK(light.size() >= 8);
    CHECK(light.size() <= 10);
    for (std::size_t i = 0; i < light.size(); ++i) {
        CHECK(light[i].second.event == HapticEvent::scrollStep);
        CHECK(light[i].second.ordinal == 3);  // Hover, because the rule itself is Step
        if (i > 0) CHECK(light[i].first - light[i - 1].first >= 0.04 - 1e-9);
    }
    WheelFeelEngine e2;
    auto p2 = P();
    p2.freeSpin = false;
    p2.scrollStepOrdinal = 10;  // Click rule: light tick is Step
    t = 6.0;
    std::optional<WheelPulse> firstLight;
    for (int i = 0; i < 8 && !firstLight; ++i, t += 0.0333) {
        for (auto& w : e2.notch(1, t, p2)) {
            if (w.boost == -1) firstLight = w;
        }
    }
    REQUIRE(firstLight.has_value());
    CHECK(firstLight->ordinal == 6);
}

TEST_CASE("wheel: a reversal within a second is one Collide bump and resets the accumulator") {
    WheelFeelEngine e;
    auto p = P();
    p.scrollStepEvery = 2;
    e.notch(1, 1.0, p);
    auto a = e.notch(1, 1.2, p);  // accumulator reaches 2: a detent
    REQUIRE(a.size() == 1);
    e.notch(1, 1.4, p);           // accumulator 1
    auto r = e.notch(-1, 1.6, p);  // reversal
    REQUIRE(r.size() == 1);
    CHECK(r[0].event == HapticEvent::scrollStep);
    CHECK(r[0].ordinal == 4);
    CHECK(r[0].boost == 1);
    auto after = e.notch(-1, 1.8, p);  // accumulator was reset: 1 of 2, nothing yet
    CHECK(after.empty());
    auto then = e.notch(-1, 2.0, p);
    REQUIRE(then.size() == 1);
    CHECK(isFirm(then[0], 0));
}

TEST_CASE("wheel: scrollStepEvery spaces the detents and a +2 report counts twice") {
    WheelFeelEngine e;
    auto p = P();
    p.scrollStepEvery = 2;
    CHECK(e.notch(1, 1.0, p).empty());
    CHECK(e.notch(1, 1.3, p).size() == 1);
    CHECK(e.notch(2, 1.6, p).size() == 1);  // two notches in one report
    CHECK(e.notch(1, 1.9, p).empty());
}

TEST_CASE("wheel: a flick enters free-spin once, textures every N notches, and rests back") {
    WheelFeelEngine e;
    auto p = P();  // threshold 18/s
    int on = 0, texture = 0, other = 0;
    double t = 3.0;
    for (int i = 0; i < 20; ++i, t += 0.04) {  // 25 notches per second
        for (auto& w : e.notch(1, t, p)) {
            if (w.event == HapticEvent::freeSpinOn) ++on;
            else if (w.event == HapticEvent::freeSpinTexture) ++texture;
            else ++other;
        }
    }
    CHECK(on == 1);
    CHECK(e.freeSpinActive());
    CHECK(texture >= 6);
    CHECK(texture <= 8);  // every second notch once spinning
    CHECK(other >= 1);    // the firm ticks before the threshold
    CHECK_FALSE(e.idle(t + 0.1, p).has_value());  // still within the 300 ms rest window
    auto off = e.idle(t + 0.35, p);
    REQUIRE(off.has_value());
    CHECK(off->event == HapticEvent::freeSpinOff);
    CHECK_FALSE(e.freeSpinActive());
    CHECK_FALSE(e.idle(t + 0.5, p).has_value());
}

TEST_CASE("wheel: free-spin off never enters, and the texture pulse is paced to 40 ms") {
    WheelFeelEngine e;
    auto p = P();
    p.freeSpin = false;
    double t = 3.0;
    for (int i = 0; i < 30; ++i, t += 0.02) {
        for (auto& w : e.notch(1, t, p)) CHECK(w.event != HapticEvent::freeSpinOn);
    }
    CHECK_FALSE(e.freeSpinActive());
    WheelFeelEngine f;
    auto q = P();
    q.freeSpinTextureEvery = 1;
    t = 4.0;
    std::vector<double> textureTimes;
    for (int i = 0; i < 40; ++i, t += 0.02) {  // 50/s: free-spins at once, texture wants every notch
        for (auto& w : f.notch(1, t, q)) {
            if (w.event == HapticEvent::freeSpinTexture) textureTimes.push_back(t);
        }
    }
    REQUIRE(textureTimes.size() >= 10);
    for (std::size_t i = 1; i < textureTimes.size(); ++i) CHECK(textureTimes[i] - textureTimes[i - 1] >= 0.04 - 1e-9);
}

TEST_CASE("wheel: turning free-spin off mid-spin drops out silently") {
    WheelFeelEngine e;
    auto p = P();
    double t = 3.0;
    for (int i = 0; i < 10; ++i, t += 0.04) e.notch(1, t, p);
    REQUIRE(e.freeSpinActive());
    p.freeSpin = false;
    auto out = e.notch(1, t, p);
    CHECK_FALSE(e.freeSpinActive());
    for (auto& w : out) CHECK(w.event != HapticEvent::freeSpinOff);
}

TEST_CASE("wheel: disabled returns nothing and leaves free-spin") {
    WheelFeelEngine e;
    auto p = P();
    double t = 3.0;
    for (int i = 0; i < 10; ++i, t += 0.04) e.notch(1, t, p);
    REQUIRE(e.freeSpinActive());
    p.enabled = false;
    CHECK(e.notch(1, t, p).empty());
    CHECK_FALSE(e.freeSpinActive());
    CHECK_FALSE(e.idle(t + 1, p).has_value());
}

TEST_CASE("wheel: with the dynamic ratchet off every detent is the plain rule and reversals do not bump") {
    WheelFeelEngine e;
    auto p = P();
    p.ratchetDynamic = false;
    p.freeSpin = false;
    double t = 1.0;
    for (int i = 0; i < 10; ++i, t += 0.02) {
        auto out = e.notch(i % 2 ? 1 : -1, t, p);
        REQUIRE(out.size() == 1);
        CHECK(isFirm(out[0], 0));
    }
}

TEST_CASE("wheel: reset forgets everything") {
    WheelFeelEngine e;
    auto p = P();
    double t = 3.0;
    for (int i = 0; i < 10; ++i, t += 0.04) e.notch(1, t, p);
    e.reset();
    CHECK_FALSE(e.freeSpinActive());
    auto a = e.notch(1, t + 0.04, p);
    REQUIRE(a.size() == 1);
    CHECK(isFirm(a[0], 1));  // reads as the first notch after a pause
}
