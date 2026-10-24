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
    CHECK(d.inFlight(t));
    CHECK(d.key(ixpat::kVkS, true, t) == O::press);
    CHECK(d.pressed());
    CHECK(d.key(ixpat::kVkS, true, t + 0.01) == O::none);  // key repeat: not a second press
    CHECK(d.key(ixpat::kVkS, false, t + 0.05) == O::none);
    CHECK(d.key(ixpat::kVkLShift, false, t + 0.05) == O::none);
    CHECK(d.pressed());  // until the last key of the chord is up
    CHECK(d.key(ixpat::kVkLWin, false, t + 0.06) == O::none);
    CHECK_FALSE(d.pressed());
    CHECK_FALSE(d.inFlight(t + 0.07));
    CHECK(d.chordActive(t + 0.3));       // keys from the mouse in flight: swallow Win-chord keys
    CHECK_FALSE(d.chordActive(t + 0.6));  // 0.4 s after the last mouse key
    CHECK_FALSE(d.swallowLeftClick(t + 0.1));  // a press hides no click
}

TEST_CASE("chord: the chord is recognised whatever order kbdhid emits the makes") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkS, true, 1.0) == O::none);        // S before Shift: not yet
    CHECK(d.key(ixpat::kVkLShift, true, 1.0) == O::press);  // the modifier completes it
    d.key(ixpat::kVkS, false, 1.1);
    d.key(ixpat::kVkLShift, false, 1.1);
    CHECK(d.key(ixpat::kVkLWin, false, 1.1) == O::none);    // never a double press
    CHECK_FALSE(d.swallowLeftClick(1.2));
    D e;
    e.key(ixpat::kVkLShift, true, 2.0);
    e.key(ixpat::kVkF23, true, 2.0);
    CHECK(e.key(ixpat::kVkLWin, true, 2.0) == O::holdBegin);  // Win last
    e.key(ixpat::kVkF23, false, 3.0);
    e.key(ixpat::kVkLShift, false, 3.0);
    CHECK(e.key(ixpat::kVkLWin, false, 3.0) == O::holdEnd);
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
    CHECK_FALSE(d.inFlight(1.0));
    CHECK(d.key(ixpat::kVkLShift, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkS, true, 1.0) == O::none);  // Shift+S alone: still not Win+Shift+S
    CHECK_FALSE(d.inFlight(1.0));
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
    CHECK_FALSE(d.swallowLeftClick(3.1));  // a hold hides no click
}

TEST_CASE("chord: the firmware's hold keeps repeating its modifiers; the chord stays one hold") {
    D d;
    d.key(ixpat::kVkShift, true, 2.0);
    d.key(ixpat::kVkLWin, true, 2.0);
    CHECK(d.key(ixpat::kVkF23, true, 2.001) == O::holdBegin);
    for (int i = 1; i < 40; ++i) {
        CHECK(d.key(ixpat::kVkShift, true, 2.0 + i * 0.03) == O::none);
        CHECK(d.key(ixpat::kVkF23, true, 2.0 + i * 0.03) == O::none);
    }
    CHECK(d.pressed(3.2));  // repeats keep it fresh: not stale
    d.key(ixpat::kVkF23, false, 3.3);
    d.key(ixpat::kVkShift, false, 3.3);
    CHECK(d.key(ixpat::kVkLWin, false, 3.3) == O::holdEnd);
}

TEST_CASE("chord: Win + fake left click is one double press, click swallowed") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 4.0) == O::none);
    CHECK(d.pressed());
    CHECK(d.swallowLeftClick(4.01));
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
    CHECK(d.key(ixpat::kVkLWin, false, 5.03) == O::none);  // the release promises the click
    CHECK(d.swallowLeftClick(5.05));                       // the late fake click is still hidden
    CHECK(d.mouseLeftDown(5.05) == O::doublePress);        // and it is what decides
    CHECK(d.mouseLeftDown(5.1) == O::none);                // nothing fires twice
    CHECK_FALSE(d.swallowLeftClick(5.6));
}

TEST_CASE("chord: a Win chord with no click at all hides clicks for half a second, then nothing") {
    D d;
    d.key(ixpat::kVkLWin, true, 5.0);
    CHECK(d.key(ixpat::kVkLWin, false, 5.03) == O::none);
    CHECK(d.swallowLeftClick(5.5));
    CHECK_FALSE(d.swallowLeftClick(5.54));
    CHECK(d.mouseLeftDown(5.6) == O::none);  // a real click after the window: the user's
}

TEST_CASE("chord: Win+Shift whose letter never reached the decoder fires nothing and hides no click") {
    // The keyboard hook was down, or the letter passed before the chord was attributed: Windows acted
    // on Win+Shift+S itself. The decoder heard only the modifiers, so it must stay silent.
    D d;
    CHECK(d.key(ixpat::kVkShift, true, 1.000) == O::none);
    CHECK(d.key(ixpat::kVkLWin, true, 1.001) == O::none);
    CHECK(d.inFlight(1.01));
    CHECK(d.key(ixpat::kVkShift, false, 1.020) == O::none);
    CHECK(d.key(ixpat::kVkLWin, false, 1.021) == O::none);
    CHECK(d.key(ixpat::kVkS, false, 1.022) == O::none);  // the break of a make Windows got instead
    CHECK_FALSE(d.inFlight(1.03));
    CHECK_FALSE(d.swallowLeftClick(1.1));
    CHECK(d.mouseLeftDown(1.1) == O::none);
    CHECK(d.mouseLeftDown(1.3) == O::none);
    CHECK_FALSE(d.pressed());
}

TEST_CASE("chord: Shift arriving after Win still marks the chord as a press or hold, not the click chord") {
    D d;
    d.key(ixpat::kVkLWin, true, 2.0);
    d.key(ixpat::kVkShift, true, 2.001);
    d.key(ixpat::kVkShift, false, 2.02);
    CHECK(d.key(ixpat::kVkLWin, false, 2.021) == O::none);
    CHECK_FALSE(d.swallowLeftClick(2.1));
}

TEST_CASE("chord: consumed press hides the following fake click and does not fire") {
    D d;
    CHECK(d.key(ixpat::kVkLWin, true, 6.0) == O::none);
    d.consumePress(6.0);
    CHECK_FALSE(d.pressed());
    CHECK(d.inFlight(6.1));  // its letter, if one comes, is still the mouse's and still hidden
    CHECK(d.swallowLeftClick(6.2));
    CHECK(d.mouseLeftDown(6.2) == O::none);
    CHECK(d.key(ixpat::kVkLWin, false, 6.3) == O::none);
}

TEST_CASE("chord: a consumed Win chord released before its click hides the click without firing") {
    D d;
    d.key(ixpat::kVkLWin, true, 6.0);
    d.consumePress(6.0);
    CHECK(d.key(ixpat::kVkLWin, false, 6.03) == O::none);
    CHECK(d.swallowLeftClick(6.1));
    CHECK(d.mouseLeftDown(6.1) == O::none);
}

TEST_CASE("chord: a consumed snip press still swallows its own keys but fires nothing") {
    D d;
    d.key(ixpat::kVkLWin, true, 7.0);
    d.key(ixpat::kVkLShift, true, 7.0);
    d.consumePress(7.0);  // ring open in toggle mode: the press commits it instead
    CHECK(d.key(ixpat::kVkS, true, 7.0) == O::none);
    CHECK(d.chordActive(7.1));
    CHECK(d.inFlight(7.1));
    d.key(ixpat::kVkS, false, 7.05);
    d.key(ixpat::kVkLShift, false, 7.05);
    CHECK(d.key(ixpat::kVkLWin, false, 7.06) == O::none);
}

TEST_CASE("chord: chordActive follows any mouse key, inFlight only a Win chord, pressed() only a recognised one") {
    D d;
    CHECK_FALSE(d.chordActive(1.0));
    d.key(ixpat::kVkLShift, true, 1.0);
    CHECK(d.chordActive(1.2));
    CHECK_FALSE(d.inFlight(1.2));
    CHECK_FALSE(d.pressed());
    d.key(ixpat::kVkLShift, false, 1.2);
    CHECK(d.chordActive(1.59));
    CHECK_FALSE(d.chordActive(1.61));
}

TEST_CASE("chord: a Win chord whose release never arrives stops hiding left clicks once the mouse goes quiet") {
    D d;
    d.key(0x5B, true, 1.0);  // Win down, then nothing (the break was lost)
    CHECK(d.swallowLeftClick(1.1));
    CHECK(d.inFlight(1.1));
    CHECK_FALSE(d.swallowLeftClick(3.0));  // 2 s without a key from the mouse: the chord is stale
    CHECK_FALSE(d.chordActive(3.0));
    CHECK_FALSE(d.inFlight(3.0));
    CHECK_FALSE(d.pressed(3.0));            // and it no longer glows as held
    CHECK(d.key(0x5B, true, 3.0) == O::none);  // the next Win chord starts fresh
    CHECK(d.pressed());
}

TEST_CASE("chord: a break without its make (S up alone) is harmless and ends nothing") {
    D d;
    CHECK(d.key(0x53, false, 1.0) == O::none);
    CHECK_FALSE(d.chordActive(2.0));
}

TEST_CASE("chord: with the ring open, the press commits at its letter and the double press at its click, never at Win") {
    D d;
    int asked = 0;
    d.pressIntercept = [&] { ++asked; return true; };
    CHECK(d.key(ixpat::kVkLWin, true, 1.0) == O::none);
    CHECK(d.key(ixpat::kVkShift, true, 1.0) == O::none);
    CHECK(asked == 0);  // Windows may still get this chord if the hook dies now: nothing is committed yet
    CHECK(d.key(ixpat::kVkS, true, 1.001) == O::none);
    CHECK(asked == 1);
    CHECK_FALSE(d.pressed());
    d.key(ixpat::kVkS, false, 1.02);
    d.key(ixpat::kVkShift, false, 1.02);
    CHECK(d.key(ixpat::kVkLWin, false, 1.021) == O::none);
    D e;
    e.pressIntercept = [&] { ++asked; return true; };
    e.key(ixpat::kVkLWin, true, 2.0);
    CHECK(asked == 1);
    CHECK(e.mouseLeftDown(2.02) == O::none);  // the click is the commit
    CHECK(asked == 2);
    CHECK(e.key(ixpat::kVkLWin, false, 2.1) == O::none);
    D f;
    f.pressIntercept = [&] { ++asked; return false; };  // ring closed: the gesture fires as usual
    f.key(ixpat::kVkShift, true, 3.0);
    f.key(ixpat::kVkLWin, true, 3.0);
    CHECK(f.key(ixpat::kVkF23, true, 3.0) == O::holdBegin);
    CHECK(asked == 3);
}

TEST_CASE("chord: Win+Shift down hides no click, and a click then is the user's own") {
    D d;
    d.key(ixpat::kVkShift, true, 1.0);
    d.key(ixpat::kVkLWin, true, 1.001);
    CHECK(d.inFlight(1.01));
    CHECK_FALSE(d.swallowLeftClick(1.01));
    CHECK(d.mouseLeftDown(1.01) == O::none);
    CHECK(d.key(ixpat::kVkS, true, 1.02) == O::press);  // the press still completes
    D e;  // Shift after Win: the same
    e.key(ixpat::kVkLWin, true, 2.0);
    CHECK(e.swallowLeftClick(2.0));
    e.key(ixpat::kVkShift, true, 2.001);
    CHECK_FALSE(e.swallowLeftClick(2.002));
    CHECK(e.mouseLeftDown(2.003) == O::none);
}

TEST_CASE("chord: once a hold is recognised, a stray S does not turn it into a press") {
    D d;
    d.key(ixpat::kVkShift, true, 1.0);
    d.key(ixpat::kVkLWin, true, 1.0);
    CHECK(d.key(ixpat::kVkF23, true, 1.001) == O::holdBegin);
    CHECK(d.key(ixpat::kVkS, true, 1.5) == O::none);  // the user's own key, fed by mistake
    d.key(ixpat::kVkS, false, 1.6);
    d.key(ixpat::kVkF23, false, 2.0);
    d.key(ixpat::kVkShift, false, 2.0);
    CHECK(d.key(ixpat::kVkLWin, false, 2.0) == O::holdEnd);
}

TEST_CASE("chord: a hold whose keys go quiet expires with a holdEnd; a press or Win alone expires silently") {
    D d;
    d.key(ixpat::kVkShift, true, 1.0);
    d.key(ixpat::kVkLWin, true, 1.0);
    CHECK(d.key(ixpat::kVkF23, true, 1.0) == O::holdBegin);
    CHECK(d.expire(1.5) == O::none);      // still fresh
    CHECK(d.expire(3.0) == O::holdEnd);   // 2 s of silence: the hold is over
    CHECK_FALSE(d.pressed());
    CHECK(d.expire(3.1) == O::none);      // only once
    D e;
    e.key(ixpat::kVkLWin, true, 1.0);
    CHECK(e.expire(3.0) == O::none);
    CHECK_FALSE(e.inFlight(3.0));
}

TEST_CASE("chord: once classified, a chord wants only its own letter") {
    D d;
    CHECK_FALSE(d.wantsLetter(ixpat::kVkS));  // no chord: nothing is wanted
    d.key(ixpat::kVkShift, true, 1.0);
    d.key(ixpat::kVkLWin, true, 1.0);
    CHECK(d.wantsLetter(ixpat::kVkS));   // Win alone: either letter completes it
    CHECK(d.wantsLetter(ixpat::kVkF23));
    d.key(ixpat::kVkF23, true, 1.0);
    CHECK(d.wantsLetter(ixpat::kVkF23));
    CHECK_FALSE(d.wantsLetter(ixpat::kVkS));  // a hold: S is the user's
}
