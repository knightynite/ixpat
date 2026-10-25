#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "core/events.h"

using ixpat::HapticEvent;
using ixpat::HapticRule;

TEST_CASE("events: raw names round-trip and match Swift") {
    CHECK(ixpat::rawName(HapticEvent::menuHighlight) == "menuHighlight");
    CHECK(ixpat::rawName(HapticEvent::freeSpinTexture) == "freeSpinTexture");
    CHECK(ixpat::hapticEventFromRaw("forceClick") == HapticEvent::forceClick);
    CHECK_FALSE(ixpat::hapticEventFromRaw("nope").has_value());
    CHECK_FALSE(ixpat::hapticEventFromRaw("").has_value());
    for (std::size_t i = 0; i < ixpat::kHapticEventCount; ++i) {
        auto e = static_cast<HapticEvent>(i);
        CHECK(ixpat::hapticEventFromRaw(ixpat::rawName(e)) == e);
        CHECK_FALSE(ixpat::title(e).empty());
        CHECK_FALSE(ixpat::detail(e).empty());
    }
}

TEST_CASE("events: there are 33 events, in Swift's declaration order") {
    CHECK(ixpat::kHapticEventCount == 33);
    CHECK(static_cast<int>(HapticEvent::leftDown) == 0);
    CHECK(static_cast<int>(HapticEvent::scrollStep) == 5);
    CHECK(static_cast<int>(HapticEvent::forceClick) == 32);
}

TEST_CASE("events: titles match Settings.swift") {
    CHECK(ixpat::title(HapticEvent::leftDown) == "Left click");
    CHECK(ixpat::title(HapticEvent::scrollStep) == "Scroll notch");
    CHECK(ixpat::title(HapticEvent::pluginPulse) == "App plugin pulse");
}

TEST_CASE("events: defaults match Settings.swift") {
    CHECK(ixpat::defaultRule(HapticEvent::leftDown) == HapticRule{false, 10, 2, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::leftUp) == HapticRule{false, 3, 1, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::middleDown) == HapticRule{true, 10, 3, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::scrollStep) == HapticRule{false, 6, 2, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::screenEdge) == HapticRule{true, 4, 3, std::nullopt});
    // Two deliberate departures from the Mac's defaults, asked for on the real mouse: the action button is a crisp
    // tap with a lighter second one instead of a single strong buzz, and the ratchet's re-engage hum is off.
    CHECK(ixpat::defaultRule(HapticEvent::actionButton) == HapticRule{true, 10, 3, ixpat::HapticEcho{10, 2, 65}});
    CHECK(ixpat::defaultRule(HapticEvent::freeSpinOff) == HapticRule{false, 5, 2, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::textSelect) == HapticRule{true, 3, 1, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::leftHold) == HapticRule{false, 7, 2, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::hoverAffordance) == HapticRule{false, 3, 1, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::sliderTick) == HapticRule{true, 6, 1, std::nullopt});
    CHECK(ixpat::defaultRule(HapticEvent::forceClick) == HapticRule{true, 10, 4, std::nullopt});
}

TEST_CASE("events: rules compare by value including the echo") {
    HapticRule a{true, 10, 2, std::nullopt};
    HapticRule b = a;
    CHECK(a == b);
    b.echo = ixpat::HapticEcho{4, 1, 45};
    CHECK_FALSE(a == b);
}
