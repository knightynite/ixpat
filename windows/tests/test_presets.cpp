#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "core/presets.h"
#include "core/settings.h"

using ixpat::HapticEvent;

static const ixpat::ClickFeel& clickFeel(std::string_view name) {
    for (const auto& f : ixpat::clickFeels()) {
        if (f.name == name) return f;
    }
    FAIL("no click feel " << name);
    return ixpat::clickFeels()[0];
}
static const ixpat::WheelFeel& wheelFeel(std::string_view name) {
    for (const auto& f : ixpat::wheelFeels()) {
        if (f.name == name) return f;
    }
    FAIL("no wheel feel " << name);
    return ixpat::wheelFeels()[0];
}

TEST_CASE("presets: the Swift tables are complete and in order") {
    REQUIRE(ixpat::clickFeels().size() == 10);
    REQUIRE(ixpat::wheelFeels().size() == 9);
    CHECK(ixpat::clickFeels()[0].name == "Firmware only");
    CHECK(ixpat::clickFeels()[9].name == "Heavy hold");
    CHECK(ixpat::wheelFeels()[0].name == "Classic ratchet");
    CHECK(ixpat::wheelFeels()[8].name == "Silent");
    for (const auto& f : ixpat::clickFeels()) CHECK_FALSE(f.blurb.empty());
    for (const auto& f : ixpat::wheelFeels()) CHECK_FALSE(f.blurb.empty());
}

TEST_CASE("presets: the defaults read Custom for clicks and Silent for the wheel, as on the Mac") {
    auto s = ixpat::defaultSettings();
    CHECK(ixpat::clickFeelName(s, false) == "Custom");
    CHECK(ixpat::clickFeelName(s, true) == "Custom");
    CHECK(ixpat::wheelFeelName(s) == "Silent");  // default scrollStep is disabled {6,2}: exactly Silent
    ixpat::applyClickFeel(s, clickFeel("Firmware only"), false);
    ixpat::applyWheelFeel(s, wheelFeel("Classic ratchet"));
    CHECK(ixpat::clickFeelName(s, false) == "Firmware only");
    CHECK(ixpat::wheelFeelName(s) == "Classic ratchet");
}

TEST_CASE("presets: applying a click feel writes three rules for that button only") {
    auto s = ixpat::defaultSettings();
    ixpat::applyClickFeel(s, clickFeel("Mechanical"), false);
    CHECK(s.baseRule(HapticEvent::leftDown) == ixpat::HapticRule{true, 10, 3, std::nullopt});
    CHECK(s.baseRule(HapticEvent::leftUp) == ixpat::HapticRule{true, 6, 2, std::nullopt});
    CHECK(s.baseRule(HapticEvent::leftHold).enabled == false);
    CHECK(s.baseRule(HapticEvent::rightDown) == ixpat::defaultRule(HapticEvent::rightDown));
    CHECK(ixpat::clickFeelName(s, false) == "Mechanical");
    CHECK(ixpat::clickFeelName(s, true) == "Custom");  // the right button is untouched (defaults)
    s.baseRule(HapticEvent::leftUp).intensity = 4;
    CHECK(ixpat::clickFeelName(s, false) == "Custom");
}

TEST_CASE("presets: Deep carries an echo and Heavy hold a hold swell") {
    auto s = ixpat::defaultSettings();
    ixpat::applyClickFeel(s, clickFeel("Deep"), true);
    REQUIRE(s.baseRule(HapticEvent::rightDown).echo.has_value());
    CHECK(*s.baseRule(HapticEvent::rightDown).echo == ixpat::HapticEcho{4, 1, 45});
    ixpat::applyClickFeel(s, clickFeel("Heavy hold"), false);
    CHECK(s.baseRule(HapticEvent::leftHold) == ixpat::HapticRule{true, 7, 4, ixpat::HapticEcho{8, 2, 230}});
    CHECK(ixpat::clickFeelName(s, true) == "Deep");
    CHECK(ixpat::clickFeelName(s, false) == "Heavy hold");
}

TEST_CASE("presets: applying a wheel feel sets the ratchet, free-spin and texture") {
    auto s = ixpat::defaultSettings();
    ixpat::applyWheelFeel(s, wheelFeel("Heavy"));
    CHECK(s.baseRule(HapticEvent::scrollStep) == ixpat::HapticRule{true, 4, 3, std::nullopt});
    CHECK(s.scrollStepEvery == 1);
    CHECK_FALSE(s.ratchetDynamic);
    CHECK_FALSE(s.freeSpin);
    CHECK(ixpat::wheelFeelName(s) == "Heavy");
    ixpat::applyWheelFeel(s, wheelFeel("Coarse"));
    CHECK(s.scrollStepEvery == 2);
    CHECK(s.freeSpinTextureEvery == 3);
    CHECK(s.baseRule(HapticEvent::freeSpinTexture) == ixpat::HapticRule{true, 6, 1, std::nullopt});
    CHECK(ixpat::wheelFeelName(s) == "Coarse");
    s.ratchetLightRate = 11;
    CHECK(ixpat::wheelFeelName(s) == "Custom");
    ixpat::applyWheelFeel(s, wheelFeel("Rumble"));
    REQUIRE(s.baseRule(HapticEvent::scrollStep).echo.has_value());
    CHECK(s.baseRule(HapticEvent::scrollStep).echo->delayMs == 30);
}

TEST_CASE("presets: feel names use the base rules, not a profile override") {
    auto s = ixpat::defaultSettings();
    ixpat::applyClickFeel(s, clickFeel("Crisp"), false);
    ixpat::AppProfile p;
    p.id = ixpat::newUuidUpper();
    p.bundleID = "x.exe";
    p.rules["leftDown"] = ixpat::HapticRule{true, 4, 4, std::nullopt};
    s.profiles.push_back(p);
    s.frontBundleID = "x.exe";
    CHECK(s.rule(HapticEvent::leftDown).ordinal == 4);
    CHECK(ixpat::clickFeelName(s, false) == "Crisp");
}
