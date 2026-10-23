#include <doctest/doctest.h>

#include <cmath>
#include <ostream>

#include "core/kinetic.h"

using ixpat::Kinetic;
using ixpat::KineticConfig;

namespace {

constexpr double kFrame = 1.0 / 120;

struct Travel {
    int dy = 0, dx = 0;
    int frames = 0;
};

// Ticks at 120 Hz from t0 until the coast reports itself done (or 5 s pass).
Travel runToRest(Kinetic& k, double t0) {
    Travel tr;
    for (double t = t0; t < t0 + 5; t += kFrame) {
        auto s = k.tick(t);
        tr.dy += s.dy;
        tr.dx += s.dx;
        ++tr.frames;
        if (!s.active) break;
    }
    return tr;
}

KineticConfig enabled() {
    KineticConfig c;
    c.enabled = true;
    return c;
}

}  // namespace

TEST_CASE("kinetic: a lone slow notch travels pxPerNotch and settles quickly") {
    Kinetic k;
    auto c = enabled();
    c.pxPerNotch = 60;
    k.notch(1, 0, 10.0, c);
    CHECK(k.active());
    auto tr = runToRest(k, 10.0 + kFrame);
    CHECK(tr.dy >= 56);  // the last ~2 px (vel*tau below the 40 px/s stop) are dropped, as on the Mac
    CHECK(tr.dy <= 60);
    CHECK(tr.dx == 0);
    CHECK(tr.frames < 60);  // under half a second
    CHECK_FALSE(k.active());
}

TEST_CASE("kinetic: pxPerNotch scales the distance") {
    Kinetic k;
    auto c = enabled();
    c.pxPerNotch = 120;
    k.notch(1, 0, 10.0, c);
    auto tr = runToRest(k, 10.0 + kFrame);
    CHECK(tr.dy >= 117);
    CHECK(tr.dy <= 120);
}

TEST_CASE("kinetic: a flick coasts further than the sum of its notches") {
    Kinetic k;
    auto c = enabled();
    c.momentum = 6;
    c.accel = 1;
    double t = 20.0;
    for (int i = 0; i < 12; ++i) {
        k.notch(1, 0, t, c);
        t += 0.03;  // ~33 notches per second
    }
    auto tr = runToRest(k, t);
    CHECK(tr.dy > 12 * 60);
    CHECK(tr.frames > 30);  // keeps coasting after the wheel stops
}

TEST_CASE("kinetic: momentum 0 gives no coast beyond the notches' own distance") {
    Kinetic k;
    auto c = enabled();
    c.momentum = 0;
    c.accel = 0;
    double t = 20.0;
    for (int i = 0; i < 12; ++i) {
        k.notch(1, 0, t, c);
        t += 0.03;
    }
    auto tr = runToRest(k, t);
    CHECK(tr.dy <= 12 * 60 + 2);
}

TEST_CASE("kinetic: reversal kills the coast and starts one plain notch") {
    Kinetic k;
    auto c = enabled();
    double t = 30.0;
    for (int i = 0; i < 10; ++i) {
        k.notch(1, 0, t, c);
        t += 0.03;
    }
    k.notch(-1, 0, t, c);
    auto tr = runToRest(k, t + kFrame);
    CHECK(tr.dy <= -56);
    CHECK(tr.dy >= -62);
}

TEST_CASE("kinetic: a +2 notch event is capped at 2 and never bursts") {
    Kinetic k;
    auto c = enabled();
    k.notch(240.0 / 120, 0, 40.0, c);
    auto tr = runToRest(k, 40.0 + kFrame);
    CHECK(tr.dy <= 122);
    CHECK(tr.dy >= 116);
    Kinetic big;
    big.notch(50, 0, 41.0, c);  // a corrupt report
    auto tb = runToRest(big, 41.0 + kFrame);
    CHECK(tb.dy <= 122);
}

TEST_CASE("kinetic: horizontal notches drive dx") {
    Kinetic k;
    auto c = enabled();
    k.notch(0, 1, 50.0, c);
    auto tr = runToRest(k, 50.0 + kFrame);
    CHECK(tr.dx >= 56);
    CHECK(tr.dy == 0);
}

TEST_CASE("kinetic: reverse flips sign and the multiplier scales a free-spin") {
    Kinetic k;
    auto c = enabled();
    c.reverse = true;
    k.notch(1, 0, 60.0, c);
    auto s = k.tick(60.0 + kFrame);
    CHECK(s.dy < 0);
    Kinetic m;
    auto cm = enabled();
    cm.multiplier = 3;
    m.notch(1, 0, 70.0, cm);
    auto tm = runToRest(m, 70.0 + kFrame);
    CHECK(tm.dy >= 175);
    CHECK(tm.dy <= 180);
}

TEST_CASE("kinetic: stop() ends the coast at once and reset() forgets the notch history") {
    Kinetic k;
    auto c = enabled();
    k.notch(1, 0, 80.0, c);
    k.stop();
    auto s = k.tick(80.0 + kFrame);
    CHECK_FALSE(s.active);
    CHECK(s.dy == 0);
    k.reset();
    CHECK_FALSE(k.active());
}

TEST_CASE("kinetic: a late timer does not slow the coast (dt is integrated, then clamped to 50 ms)") {
    Kinetic a, b;
    auto c = enabled();
    a.notch(1, 0, 90.0, c);
    b.notch(1, 0, 90.0, c);
    int da = 0, db = 0;
    for (double t = 90.0 + kFrame; t < 91.0; t += kFrame) da += a.tick(t).dy;
    for (double t = 90.0 + 0.04; t < 91.0; t += 0.04) db += b.tick(t).dy;  // 25 Hz
    CHECK(std::abs(da - db) <= 2);
}

TEST_CASE("kinetic: ticking with nothing in flight is inert") {
    Kinetic k;
    auto s = k.tick(1.0);
    CHECK_FALSE(s.active);
    CHECK(s.dy == 0);
    CHECK(s.dx == 0);
}
