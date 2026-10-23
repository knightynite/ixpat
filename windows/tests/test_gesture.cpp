#include <doctest/doctest.h>

#include <ostream>

#include "core/gesture.h"

using ixpat::ActionKind;
using ixpat::ButtonAction;
using ixpat::GestureAccumulator;
using ixpat::GestureConfig;

namespace {
GestureConfig cfg() {
    GestureConfig c;
    c.up = ButtonAction::blank(ActionKind::volumeUp);
    c.down = ButtonAction::blank(ActionKind::volumeDown);
    c.left = ButtonAction::blank(ActionKind::previousTrack);
    c.right = ButtonAction::blank(ActionKind::nextTrack);
    c.stepPx = 24;
    c.swipePx = 90;
    return c;
}
}  // namespace

TEST_CASE("gesture: inactive accumulator passes motion through untouched") {
    GestureAccumulator g;
    CHECK_FALSE(g.active());
    auto r = g.move(50, 50, cfg());
    CHECK_FALSE(r.consumed);
    CHECK(r.actions.empty());
}

TEST_CASE("gesture: the first 10 px decide the axis and fire nothing") {
    GestureAccumulator g;
    g.begin();
    CHECK(g.active());
    auto r = g.move(3, 2, cfg());
    CHECK(r.consumed);
    CHECK(r.actions.empty());
    r = g.move(4, 2, cfg());  // 7,4 -> hypot ~8: still undecided
    CHECK(r.actions.empty());
    CHECK(g.firedCount() == 0);
}

TEST_CASE("gesture: vertical travel steps the volume every stepPx, up is negative dy") {
    GestureAccumulator g;
    g.begin();
    g.move(2, -12, cfg());  // axis locked vertical; accumulators reset after the lock
    auto r = g.move(0, -24, cfg());
    REQUIRE(r.actions.size() == 1);
    CHECK(r.actions[0].kind == ActionKind::volumeUp);
    r = g.move(0, 50, cfg());  // 50 px down: two steps
    REQUIRE(r.actions.size() == 2);
    CHECK(r.actions[0].kind == ActionKind::volumeDown);
    CHECK(g.firedCount() == 3);
    r = g.move(0, 10, cfg());  // 2 px carried + 10 = 12 < 24: nothing yet
    CHECK(r.actions.empty());
    r = g.move(0, 12, cfg());  // 24: one more
    CHECK(r.actions.size() == 1);
}

TEST_CASE("gesture: horizontal travel swipes every swipePx and the axis stays locked") {
    GestureAccumulator g;
    g.begin();
    g.move(12, 1, cfg());
    auto r = g.move(90, 0, cfg());
    REQUIRE(r.actions.size() == 1);
    CHECK(r.actions[0].kind == ActionKind::nextTrack);
    r = g.move(0, 300, cfg());  // vertical motion now: ignored but consumed
    CHECK(r.consumed);
    CHECK(r.actions.empty());
    r = g.move(-180, 0, cfg());
    REQUIRE(r.actions.size() == 2);
    CHECK(r.actions[0].kind == ActionKind::previousTrack);
}

TEST_CASE("gesture: at most six actions per move") {
    GestureAccumulator g;
    g.begin();
    g.move(0, 12, cfg());
    auto r = g.move(0, 24 * 20, cfg());
    CHECK(r.actions.size() == 6);
}

TEST_CASE("gesture: a zero unit never loops") {
    GestureAccumulator g;
    g.begin();
    auto c = cfg();
    c.stepPx = 0;
    g.move(0, 12, c);
    auto r = g.move(0, 500, c);
    CHECK(r.consumed);
    CHECK(r.actions.empty());
}

TEST_CASE("gesture: end() stops consuming and begin() starts fresh") {
    GestureAccumulator g;
    g.begin();
    g.move(0, 12, cfg());
    g.move(0, 24, cfg());
    CHECK(g.firedCount() == 1);
    g.end();
    CHECK_FALSE(g.active());
    CHECK(g.firedCount() == 1);  // the last hold's count survives until the next begin
    CHECK_FALSE(g.move(0, 100, cfg()).consumed);
    g.begin();
    CHECK(g.firedCount() == 0);
    CHECK(g.move(1, 1, cfg()).actions.empty());  // axis undecided again
}
