#include <doctest/doctest.h>

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "core/router.h"

using ixpat::ActionKind;
using ixpat::ButtonAction;
using ixpat::ButtonGesture;
using ixpat::ChordOutput;
using ixpat::HapticEvent;
using ixpat::Router;
using ixpat::RouterSinks;

namespace {

struct Played {
    HapticEvent e;
    std::optional<int> ordinal;
    int boost;
};

struct Rig {
    ixpat::Settings s = ixpat::defaultSettings();
    std::vector<Played> played;
    std::vector<std::pair<ActionKind, bool>> ran;
    std::vector<std::pair<int, int>> armed;
    std::vector<int> cancelled;
    int gesturesBegin = 0, gesturesEnd = 0, ringToggle = 0, ringHoldEnded = 0;
    std::vector<bool> ringOpened;
    bool ringOpen = false;
    std::optional<float> volume;
    std::unique_ptr<Router> r;

    Rig() {
        RouterSinks k;
        k.play = [this](HapticEvent e, std::optional<int> o, int b) { played.push_back({e, o, b}); };
        k.runAction = [this](const ButtonAction& a, bool hold) { ran.emplace_back(a.kind, hold); };
        k.armTimer = [this](int id, int ms) { armed.emplace_back(id, ms); };
        k.cancelTimer = [this](int id) { cancelled.push_back(id); };
        k.gesturesBegin = [this] { ++gesturesBegin; };
        k.gesturesEnd = [this] { ++gesturesEnd; };
        k.ringOpen = [this](bool hold) { ringOpened.push_back(hold); ringOpen = true; };
        k.ringToggle = [this] { ++ringToggle; ringOpen = !ringOpen; };
        k.ringIsOpen = [this] { return ringOpen; };
        k.ringHoldEnded = [this] { ++ringHoldEnded; };
        k.systemVolume = [this] { return volume; };
        r = std::make_unique<Router>(s, std::move(k));
    }
    int count(HapticEvent e) const {
        int n = 0;
        for (auto& p : played) n += p.e == e;
        return n;
    }
};

}  // namespace

TEST_CASE("router: left press plays leftDown, arms the hold timer, release plays leftUp and cancels") {
    Rig g;
    g.s.baseRule(HapticEvent::leftHold).enabled = true;
    g.r->buttons(0x00, 0x01, 1.0);
    REQUIRE(g.played.size() == 1);
    CHECK(g.played[0].e == HapticEvent::leftDown);
    REQUIRE(g.armed.size() == 1);
    CHECK(g.armed[0] == std::pair{ixpat::kTimerLeftHold, 300});
    g.r->timer(ixpat::kTimerLeftHold, 1.3);  // still down
    CHECK(g.count(HapticEvent::leftHold) == 1);
    g.r->buttons(0x01, 0x00, 1.5);
    CHECK(g.count(HapticEvent::leftUp) == 1);
    CHECK(g.cancelled.back() == ixpat::kTimerLeftHold);  // (arming also cancels any earlier timer, like the Mac)
    g.r->timer(ixpat::kTimerLeftHold, 1.6);  // a late timer after release plays nothing
    CHECK(g.count(HapticEvent::leftHold) == 1);
}

TEST_CASE("router: hold timers are only armed when the hold rule is enabled; right and wheel buttons route too") {
    Rig g;
    g.r->buttons(0x00, 0x02, 1.0);
    CHECK(g.count(HapticEvent::rightDown) == 1);
    CHECK(g.armed.empty());  // rightHold is off by default
    g.r->buttons(0x02, 0x06, 1.1);
    CHECK(g.count(HapticEvent::middleDown) == 1);
    g.r->buttons(0x06, 0x00, 1.2);
    CHECK(g.count(HapticEvent::rightUp) == 1);
    g.s.enabled = false;
    g.r->buttons(0x00, 0x01, 2.0);
    CHECK(g.count(HapticEvent::leftDown) == 0);
}

TEST_CASE("router: wheel notches go through the wheel feel and the free-spin idle timer") {
    Rig g;
    g.s.baseRule(HapticEvent::scrollStep).enabled = true;
    g.r->wheel(1, 1.0);
    CHECK(g.count(HapticEvent::scrollStep) == 1);
    CHECK(g.played.back().boost == 1);  // first notch after a pause
    double t = 2.0;
    for (int i = 0; i < 12; ++i, t += 0.04) g.r->wheel(1, t);  // 25/s: free-spin
    CHECK(g.count(HapticEvent::freeSpinOn) == 1);
    CHECK(g.r->freeSpinActive());
    bool idleArmed = false;
    for (auto& a : g.armed) idleArmed |= a.first == ixpat::kTimerFreeSpinIdle && a.second == 300;
    CHECK(idleArmed);
    g.r->timer(ixpat::kTimerFreeSpinIdle, t + 0.1);  // too early: the wheel moved 100 ms ago
    CHECK_FALSE(g.played.back().e == HapticEvent::freeSpinOff);
    g.r->timer(ixpat::kTimerFreeSpinIdle, t + 0.35);
    CHECK(g.count(HapticEvent::freeSpinOff) == 1);
    CHECK_FALSE(g.r->freeSpinActive());
}

TEST_CASE("router: wheel params come from the settings and the device table") {
    Rig g;
    auto p = g.r->wheelParams();
    CHECK(p.collideOrdinal == 4);
    CHECK(p.stepOrdinal == 6);
    CHECK(p.hoverOrdinal == 3);
    CHECK(p.scrollStepOrdinal == 6);
    CHECK(p.freeSpinThreshold == 18);
    std::vector<ixpat::Waveform> table{{20, 0x1012, 26}, {21, 0x1014, 11}, {22, 0x1008, 9}, {23, 0x100A, 147}};
    g.r->setTable(table);
    p = g.r->wheelParams();
    CHECK(p.collideOrdinal == 20);
    CHECK(p.stepOrdinal == 21);
    CHECK(p.hoverOrdinal == 22);
}

TEST_CASE("router: a press plays actionButton and runs the mapped action; the ring action toggles the ring") {
    Rig g;
    g.r->chord(ChordOutput::press, 1.0);  // default press = Action Ring
    CHECK(g.count(HapticEvent::actionButton) == 1);
    CHECK(g.ringToggle == 1);
    CHECK(g.ran.empty());
    g.r->chord(ChordOutput::doublePress, 2.0);  // default = Task View
    REQUIRE(g.ran.size() == 1);
    CHECK(g.ran[0] == std::pair{ActionKind::missionControl, false});
}

TEST_CASE("router: a hold with gesturesOnHold defers its action and starts gesture mode") {
    Rig g;  // default hold = screenshot
    g.r->chord(ChordOutput::holdBegin, 1.0);
    CHECK(g.count(HapticEvent::actionHold) == 1);  // the hold has a feel of its own
    CHECK(g.gesturesBegin == 1);
    CHECK(g.ran.empty());
    g.r->chord(ChordOutput::holdEnd, 2.0);  // no movement: the hold's own action runs
    CHECK(g.gesturesEnd == 1);
    CHECK(g.ringHoldEnded == 1);
    REQUIRE(g.ran.size() == 1);
    CHECK(g.ran[0] == std::pair{ActionKind::screenshot, true});
}

TEST_CASE("router: a hold that gestured does not run its deferred action") {
    Rig g;
    g.r->chord(ChordOutput::holdBegin, 1.0);
    g.r->gestureFired(ButtonAction::blank(ActionKind::volumeUp), 1.2);
    CHECK(g.count(HapticEvent::gestureStep) == 1);
    REQUIRE(g.ran.size() == 1);
    CHECK(g.ran[0].first == ActionKind::volumeUp);
    g.r->chord(ChordOutput::holdEnd, 2.0);
    CHECK(g.ran.size() == 1);  // screenshot did not run
}

TEST_CASE("router: a hold mapped to the Action Ring opens it in hold mode and the release commits") {
    Rig g;
    g.s.action(ButtonGesture::hold) = ButtonAction::blank(ActionKind::actionRing);
    g.r->chord(ChordOutput::holdBegin, 1.0);
    REQUIRE(g.ringOpened.size() == 1);
    CHECK(g.ringOpened[0] == true);
    CHECK(g.gesturesBegin == 0);
    g.r->chord(ChordOutput::holdEnd, 2.0);
    CHECK(g.ringHoldEnded == 1);
}

TEST_CASE("router: gesturesOnHold off runs the hold action at once") {
    Rig g;
    g.s.gesturesOnHold = false;
    g.r->chord(ChordOutput::holdBegin, 1.0);
    REQUIRE(g.ran.size() == 1);
    CHECK(g.ran[0] == std::pair{ActionKind::screenshot, true});
    CHECK(g.gesturesBegin == 0);
}

TEST_CASE("router: gesture actions play their pulses; volume at the limit bumps instead of stepping") {
    Rig g;
    g.r->chord(ChordOutput::holdBegin, 1.0);
    g.volume = 1.0f;
    g.r->gestureFired(ButtonAction::blank(ActionKind::volumeUp), 1.1);
    CHECK(g.count(HapticEvent::gestureLimit) == 1);
    CHECK(g.ran.empty());
    g.volume = 0.5f;
    g.r->gestureFired(ButtonAction::blank(ActionKind::volumeDown), 1.2);
    CHECK(g.count(HapticEvent::gestureStep) == 1);
    g.r->gestureFired(ButtonAction::blank(ActionKind::previousTrack), 1.3);
    REQUIRE(g.played.back().e == HapticEvent::gestureSwipe);
    CHECK(g.played.back().ordinal == 9);  // Error: the descending pattern
    g.r->gestureFired(ButtonAction::blank(ActionKind::nextTrack), 1.4);
    CHECK(g.played.back().e == HapticEvent::gestureSwipe);
    CHECK_FALSE(g.played.back().ordinal.has_value());
    g.r->gestureFired(ButtonAction::blank(ActionKind::brightnessUp), 1.5);
    CHECK(g.count(HapticEvent::gestureStep) == 2);
}

TEST_CASE("router: system events are forwarded and the foreground app sets the profile key") {
    Rig g;
    g.r->systemEvent(HapticEvent::screenEdge, 1.0);
    CHECK(g.count(HapticEvent::screenEdge) == 1);
    g.r->foregroundChanged("Chrome.EXE", 2.0);
    CHECK(g.s.frontBundleID == "chrome.exe");
    CHECK(g.count(HapticEvent::appSwitch) == 1);
}

TEST_CASE("router: losing the device mid-hold ends gesture mode and clears the pending action") {
    Rig g;
    g.r->chord(ChordOutput::holdBegin, 1.0);
    g.r->deviceConnected(false, 1.5);
    CHECK(g.gesturesEnd == 1);
    CHECK(g.ringHoldEnded == 1);
    g.r->chord(ChordOutput::holdEnd, 2.0);  // a late release: nothing runs
    CHECK(g.ran.empty());
}

TEST_CASE("router: a hold mapped to nothing still starts gesture mode, as on the Mac") {
    Rig g;
    g.s.action(ButtonGesture::hold) = ButtonAction::blank(ActionKind::none);
    g.r->chord(ChordOutput::holdBegin, 1.0);
    CHECK(g.gesturesBegin == 1);
    g.r->chord(ChordOutput::holdEnd, 2.0);
    CHECK(g.ran.empty());
}

TEST_CASE("router: reset ends gesture mode and clears the pending hold") {
    Rig g;
    g.r->chord(ChordOutput::holdBegin, 1.0);
    g.r->reset();
    CHECK(g.gesturesEnd == 1);
    g.r->chord(ChordOutput::holdEnd, 2.0);
    CHECK(g.ran.empty());
}

TEST_CASE("router: disabled settings ignore the action button") {
    Rig g;
    g.s.enabled = false;
    g.r->chord(ChordOutput::press, 1.0);
    CHECK(g.played.empty());
    CHECK(g.ringToggle == 0);
}
