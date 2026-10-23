#include <doctest/doctest.h>

#include <ostream>

#include "core/chord.h"

using D = ixpat::ChordDecoder;
using O = ixpat::ChordOutput;

TEST_CASE("chord: Win+Shift+S is a press, released cleanly") {
    D d;
    double t = 1.0;
    CHECK(d.key(ixpat::kVkLWin, true, t) == O::none);
    CHECK(d.key(ixpat::kVkLShift, true, t) == O::none);
    CHECK(d.pressed());  // Win alone already reads as a chord (the double-press candidate), as on the Mac
    CHECK(d.key(ixpat::kVkS, true, t) == O::press);
    CHECK(d.pressed());
    CHECK(d.key(ixpat::kVkS, true, t + 0.01) == O::none);  // key repeat: not a second press
    CHECK(d.key(ixpat::kVkS, false, t + 0.05) == O::none);
    CHECK(d.key(ixpat::kVkLShift, false, t + 0.05) == O::none);
    CHECK(d.pressed());  // until the last key of the chord is up
    CHECK(d.key(ixpat::kVkLWin, false, t + 0.06) == O::none);
    CHECK_FALSE(d.pressed());
    CHECK(d.chordActive(t + 0.3));       // keys from the mouse in flight: swallow Win-chord keys
    CHECK_FALSE(d.chordActive(t + 0.6));  // 0.4 s after the last mouse key
}

TEST_CASE("chord: right-hand modifiers count too") {
    D d;
    CHECK(d.key(ixpat::kVkRWin, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkRShift, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkS, true, 1.0) == O::press);
}

TEST_CASE("chord: S without the modifiers is not a chord") {
    D d;
    CHECK(d.key(ixpat::kVkS, true, 1.0) == O::none);
    CHECK_FALSE(d.pressed());
    CHECK(d.key(ixpat::kVkLShift, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkS, true, 1.0) == O::none);  // Shift+S alone: still not Win+Shift+S
}

TEST_CASE("chord: Win+Shift+F23 held is holdBegin, its release holdEnd") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 2.0) == O::none);
    CHECK(d.key(ixpat::kVkLShift, true, 2.0) == O::none);
    CHECK(d.key(ixpat::kVkF23, true, 2.0) == O::holdBegin);
    CHECK(d.pressed());
    for (int i = 1; i < 20; ++i) CHECK(d.key(ixpat::kVkF23, true, 2.0 + i * 0.05) == O::none);  // repeats
    CHECK(d.key(ixpat::kVkF23, false, 3.0) == O::none);
    CHECK(d.key(ixpat::kVkLShift, false, 3.0) == O::none);
    CHECK(d.key(ixpat::kVkLWin, false, 3.0) == O::holdEnd);
    CHECK_FALSE(d.pressed());
}

TEST_CASE("chord: Win + fake left click is one double press, click swallowed") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 4.0) == O::none);
    CHECK(d.pressed());
    CHECK(d.mouseLeftDown(4.02) == O::doublePress);
    CHECK(d.swallowLeftClick(4.1));
    CHECK(d.key(ixpat::kVkLWin, false, 4.1) == O::none);  // not a second one
    CHECK_FALSE(d.pressed());
    CHECK(d.swallowLeftClick(4.5));       // 0.5 s window after the click
    CHECK_FALSE(d.swallowLeftClick(4.6));
    CHECK(d.mouseLeftDown(4.7) == O::none);  // a real click later: nothing
}

TEST_CASE("chord: Win released before the click still yields exactly one double press") {
    D d;
    d.key(ixpat::kVkLWin, true, 5.0);
    CHECK(d.key(ixpat::kVkLWin, false, 5.03) == O::doublePress);
    CHECK(d.swallowLeftClick(5.05));          // the late fake click is still hidden
    CHECK(d.mouseLeftDown(5.05) == O::none);  // and does not fire again
    CHECK_FALSE(d.swallowLeftClick(5.6));
}

TEST_CASE("chord: consumed press hides the following fake click and does not fire") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 6.0) == O::none);
    d.consumePress(6.0);
    CHECK_FALSE(d.pressed());
    CHECK(d.swallowLeftClick(6.2));
    CHECK(d.mouseLeftDown(6.2) == O::none);
    CHECK(d.key(ixpat::kVkLWin, false, 6.3) == O::none);
}

TEST_CASE("chord: a consumed snip press still swallows its own keys but fires nothing") {
    D d;
    d.key(ixpat::kVkLWin, true, 7.0);
    d.key(ixpat::kVkLShift, true, 7.0);
    d.consumePress(7.0);  // ring open in toggle mode: the press commits it instead
    CHECK(d.key(ixpat::kVkS, true, 7.0) == O::none);
    CHECK(d.chordActive(7.1));
    d.key(ixpat::kVkS, false, 7.05);
    d.key(ixpat::kVkLShift, false, 7.05);
    CHECK(d.key(ixpat::kVkLWin, false, 7.06) == O::none);
}

TEST_CASE("chord: chordActive follows any mouse key, pressed() only a recognised chord") {
    D d;
    CHECK_FALSE(d.chordActive(1.0));
    d.key(ixpat::kVkLShift, true, 1.0);
    CHECK(d.chordActive(1.2));
    CHECK_FALSE(d.pressed());
    d.key(ixpat::kVkLShift, false, 1.2);
    CHECK(d.chordActive(1.59));
    CHECK_FALSE(d.chordActive(1.61));
}
