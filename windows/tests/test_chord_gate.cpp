#include <doctest/doctest.h>

#include <ostream>
#include <vector>

#include "core/chord_gate.h"

using namespace ixpat;
using O = ChordOutput;

namespace {

constexpr std::uint16_t kScanShift = 0x2A, kScanWin = 0x5B, kScanS = 0x1F, kScanF23 = 0x6E;

/// The firmware's sequences as the logs recorded them: each key's hook callback, then its Raw Input report about
/// a millisecond later (the mouse's letters never produce a report). `outs` collects everything the decoder fired.
struct Rig {
    ChordGate g;
    ChordGate::Config cfg;
    std::vector<O> outs;
    int masks = 0;

    void take(const ChordGate::Decision& d) {
        if (d.out != O::none) outs.push_back(d.out);
        if (d.sendMask) ++masks;
    }
    // A modifier from the mouse: the hook passes it, the report follows and is vouched for.
    bool modifier(int hookVk, int rawVk, std::uint16_t scan, bool down, Seconds t) {
        const auto h = g.hookKey(hookVk, down, scan, t, cfg);
        take(h);
        const auto r = g.rawKey(rawVk, down, scan, true, t + 0.001, cfg);
        take(r);
        return !h.swallow && r.hookSaw;
    }
    // A letter from the mouse: the hook alone sees it.
    ChordGate::Decision letter(int vk, std::uint16_t scan, bool down, Seconds t) {
        const auto h = g.hookKey(vk, down, scan, t, cfg);
        take(h);
        return h;
    }
    // A key from the user's keyboard: hook, then a report from another device.
    bool keyboard(int hookVk, int rawVk, std::uint16_t scan, bool down, Seconds t) {
        const auto h = g.hookKey(hookVk, down, scan, t, cfg);
        take(h);
        g.rawKey(rawVk, down, scan, false, t + 0.0004, cfg);
        return !h.swallow;
    }
};

}  // namespace

TEST_CASE("gate: the press as the firmware sends it fires once, hides only its letter, masks the Win release") {
    Rig r;
    // 06:22:47 in the log: Shift make, Win make, S make, then Shift, Win, S breaks, ~15 ms apart in the original
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.000));  // passes
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002));       // passes, and arms the chord
    CHECK(r.masks == 1);                                              // Windows holds Win: the mask goes
    const auto s = r.letter(kVkS, kScanS, true, 1.004);
    CHECK(s.swallow);
    CHECK(s.out == O::press);
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, false, 1.020));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, false, 1.022));
    CHECK(r.letter(kVkS, kScanS, false, 1.024).swallow);  // the break of an eaten make is eaten too
    CHECK(r.outs == std::vector<O>{O::press});
    CHECK(r.g.idle(1.5));  // nothing owed
    CHECK_FALSE(r.g.decoder().swallowLeftClick(1.1));  // a press hides no click
}

TEST_CASE("gate: a Win release the hook saw but Raw Input never reports (we swallowed it and sent our own) leaves nothing owed") {
    Rig r;
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.000));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002));
    CHECK(r.letter(kVkS, kScanS, true, 1.004).out == O::press);
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, false, 1.020));
    const auto up = r.g.hookKey(kVkLWin, false, kScanWin, 1.022, r.cfg);  // the gate says pass; the plumbing swallowed it
    r.take(up);
    CHECK_FALSE(up.swallow);
    r.take(r.g.rawKey(kVkLWin, false, kScanWin, true, 1.022, r.cfg));  // ...and hands the gate the report Windows will never make
    CHECK(r.letter(kVkS, kScanS, false, 1.024).swallow);
    CHECK(r.outs == std::vector<O>{O::press});
    CHECK(r.g.idle(1.5));
    // and the next press is a press again
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, true, 2.000));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 2.002));
    CHECK(r.letter(kVkS, kScanS, true, 2.004).out == O::press);
}

TEST_CASE("gate: with the keyboard hook gone, the chord's reports arrive unvouched and nothing fires") {
    Rig r;
    // 06:22:43 in the log: Raw Input saw Shift, Win, then their breaks and an S break; the hook saw nothing.
    CHECK_FALSE(r.g.rawKey(kVkShift, true, kScanShift, true, 1.000, r.cfg).hookSaw);
    CHECK_FALSE(r.g.rawKey(kVkLWin, true, kScanWin, true, 1.001, r.cfg).hookSaw);
    CHECK_FALSE(r.g.decoder().inFlight(1.002));  // Windows acted (Snipping Tool); we stay silent
    r.g.rawKey(kVkShift, false, kScanShift, true, 1.022, r.cfg);
    r.g.rawKey(kVkLWin, false, kScanWin, true, 1.023, r.cfg);
    r.g.rawKey(kVkS, false, kScanS, true, 1.024, r.cfg);
    CHECK(r.outs.empty());
    CHECK(r.masks == 0);
    CHECK_FALSE(r.g.decoder().swallowLeftClick(1.1));
}

TEST_CASE("gate: the user's own keys pass untouched: s, Win alone, Win+Shift+S from the keyboard") {
    Rig r;
    CHECK(r.keyboard(kVkS, kVkS, kScanS, true, 1.0));
    CHECK(r.keyboard(kVkS, kVkS, kScanS, false, 1.1));
    CHECK(r.keyboard(kVkLWin, kVkLWin, kScanWin, true, 2.0));
    CHECK(r.keyboard(kVkLWin, kVkLWin, kScanWin, false, 2.2));
    CHECK(r.keyboard(kVkLWin, kVkLWin, kScanWin, true, 3.0));
    CHECK(r.keyboard(kVkLShift, kVkShift, kScanShift, true, 3.05));
    CHECK(r.keyboard(kVkS, kVkS, kScanS, true, 3.10));  // Snipping Tool, as the user meant
    CHECK(r.keyboard(kVkS, kVkS, kScanS, false, 3.20));
    CHECK(r.keyboard(kVkLShift, kVkShift, kScanShift, false, 3.21));
    CHECK(r.keyboard(kVkLWin, kVkLWin, kScanWin, false, 3.22));
    CHECK(r.outs.empty());
    CHECK(r.masks == 0);
    CHECK_FALSE(r.g.decoder().inFlight(3.1));
}

TEST_CASE("gate: the double press is Win plus the fake click, hidden, fired once; later clicks are the user's") {
    Rig r;
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.000));
    CHECK(r.masks == 1);
    const auto click = r.g.leftDown(1.020, r.cfg, false);
    CHECK(click.swallow);
    CHECK(click.out == O::doublePress);
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, false, 1.040));
    CHECK_FALSE(r.g.leftDown(2.0, r.cfg, false).swallow);
    CHECK(r.g.leftDown(2.0, r.cfg, false).out == O::none);
}

TEST_CASE("gate: the hold swallows F23 and its repeats, begins at once and ends with the last key") {
    Rig r;
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.000));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002));
    const auto f = r.letter(kVkF23, kScanF23, true, 1.004);
    CHECK(f.swallow);
    CHECK(f.out == O::holdBegin);
    for (int i = 1; i <= 30; ++i) {
        const auto rep = r.letter(kVkF23, kScanF23, true, 1.004 + i * 0.03);  // typematic
        CHECK(rep.swallow);
        CHECK(rep.out == O::none);
    }
    CHECK(r.g.decoder().pressed(2.0));
    CHECK(r.letter(kVkF23, kScanF23, false, 2.0).swallow);
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, false, 2.01));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, false, 2.02));
    CHECK(r.outs == std::vector<O>{O::holdBegin, O::holdEnd});
}

TEST_CASE("gate: a letter the user already holds is never eaten, and the chord goes without it") {
    Rig r;
    CHECK(r.keyboard(kVkS, kVkS, kScanS, true, 1.0));  // the user holds s
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.5));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.502));
    CHECK_FALSE(r.g.hookKey(kVkS, true, kScanS, 1.52, r.cfg).swallow);  // the user's repeat still passes
    CHECK(r.modifier(kVkLShift, kVkShift, kScanShift, false, 1.53));
    CHECK(r.modifier(kVkLWin, kVkLWin, kScanWin, false, 1.531));
    CHECK_FALSE(r.g.hookKey(kVkS, false, kScanS, 1.8, r.cfg).swallow);  // Windows gets the release it is owed
    CHECK(r.outs.empty());  // no press: the mouse's S never came
}

TEST_CASE("gate: a classified hold leaves the user's S alone") {
    Rig r;
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002);
    CHECK(r.letter(kVkF23, kScanF23, true, 1.004).out == O::holdBegin);
    CHECK_FALSE(r.g.hookKey(kVkS, true, kScanS, 1.5, r.cfg).swallow);  // typed during the hold: passes
    CHECK_FALSE(r.g.hookKey(kVkS, false, kScanS, 1.6, r.cfg).swallow);
    r.letter(kVkF23, kScanF23, false, 2.0);
    r.modifier(kVkLShift, kVkShift, kScanShift, false, 2.01);
    r.modifier(kVkLWin, kVkLWin, kScanWin, false, 2.02);
    CHECK(r.outs == std::vector<O>{O::holdBegin, O::holdEnd});
}

TEST_CASE("gate: a hold whose breaks never come ends on its own, and the next chord starts clean") {
    Rig r;
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002);
    CHECK(r.letter(kVkF23, kScanF23, true, 1.004).out == O::holdBegin);
    CHECK(r.g.tick(1.5) == O::none);
    CHECK(r.g.tick(3.0) == O::holdEnd);  // the mouse went to sleep mid-hold
    CHECK(r.g.idle(3.0));
    // the next press works as usual
    r.outs.clear();
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 5.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 5.002);
    CHECK(r.letter(kVkS, kScanS, true, 5.004).out == O::press);
}

TEST_CASE("gate: a re-install voids earlier observations; Windows' key state settles a letter's ownership") {
    Rig r;
    r.g.hookKey(kVkLShift, true, kScanShift, 1.0, r.cfg);
    r.g.hooksReinstalled();
    CHECK_FALSE(r.g.rawKey(kVkShift, true, kScanShift, true, 1.001, r.cfg).hookSaw);
    // a swallowed S whose break the hook missed, while Windows got its own make meanwhile: Windows holds it now
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 2.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 2.002);
    CHECK(r.letter(kVkS, kScanS, true, 2.004).swallow);
    r.g.reconcileLetters(true, false);
    CHECK(r.g.letterPassed(kVkS));
    CHECK_FALSE(r.g.swallowedMake(kVkS));
    CHECK_FALSE(r.g.hookKey(kVkS, false, kScanS, 2.5, r.cfg).swallow);  // the release reaches Windows
}

TEST_CASE("gate: another keyboard's report consumes its own observation and cannot vouch for the mouse") {
    Rig r;
    r.g.hookKey(kVkLWin, true, kScanWin, 1.0, r.cfg);
    r.g.rawKey(kVkLWin, true, kScanWin, false, 1.0004, r.cfg);  // the keyboard's own report
    CHECK_FALSE(r.g.rawKey(kVkLWin, true, kScanWin, true, 1.010, r.cfg).hookSaw);  // a mouse report with no callback of its own
    CHECK_FALSE(r.g.decoder().inFlight(1.02));
}

TEST_CASE("gate: with the ring open the press commits it at the letter and the click chord at its click") {
    Rig r;
    int asked = 0;
    r.g.decoder().pressIntercept = [&] { ++asked; return true; };
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002);
    CHECK(asked == 0);
    const auto s = r.letter(kVkS, kScanS, true, 1.004);
    CHECK(s.swallow);
    CHECK(s.out == O::none);
    CHECK(asked == 1);
    r.letter(kVkS, kScanS, false, 1.02);
    r.modifier(kVkLShift, kVkShift, kScanShift, false, 1.03);
    r.modifier(kVkLWin, kVkLWin, kScanWin, false, 1.031);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 2.0);
    const auto click = r.g.leftDown(2.02, r.cfg, true);
    CHECK(click.swallow);
    CHECK(click.out == O::none);
    CHECK(asked == 2);
    CHECK(r.outs.empty());
}

TEST_CASE("gate: hiding switched off: the chord still fires and nothing is eaten") {
    Rig r;
    r.cfg.swallowChords = false;
    r.modifier(kVkLShift, kVkShift, kScanShift, true, 1.0);
    r.modifier(kVkLWin, kVkLWin, kScanWin, true, 1.002);
    CHECK(r.masks == 0);  // Windows may open Start: the user asked for stock behaviour
    const auto s = r.letter(kVkS, kScanS, true, 1.004);
    CHECK_FALSE(s.swallow);
    CHECK(s.out == O::press);
    CHECK_FALSE(r.letter(kVkS, kScanS, false, 1.02).swallow);
    r.modifier(kVkLShift, kVkShift, kScanShift, false, 1.03);
    r.modifier(kVkLWin, kVkLWin, kScanWin, false, 1.031);
    CHECK(r.outs == std::vector<O>{O::press});
    CHECK(r.g.idle(1.5));
}
