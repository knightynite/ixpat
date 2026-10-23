#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ixpat {

/// Things that can trigger a haptic pulse. Order and raw names are Settings.swift's `HapticEvent`;
/// the raw names are the keys in settings.json, so they never change.
enum class HapticEvent : std::uint8_t {
    leftDown, leftUp, rightDown, rightUp, middleDown,
    scrollStep, screenEdge, dragEnd, windowSnap, appSwitch, actionButton,
    menuHighlight, textSelect, textLine, ringDetent, ringOpen, ringCommit, freeSpinOn, freeSpinOff,
    leftHold, rightHold, freeSpinTexture, gestureStep, gestureLimit, gestureSwipe,
    controlHover, hoverAffordance, dropTarget, sliderTick, selectionChange, scrollEnd, pluginPulse,
    forceClick,
    count_
};
constexpr std::size_t kHapticEventCount = static_cast<std::size_t>(HapticEvent::count_);

std::string_view rawName(HapticEvent e);
std::optional<HapticEvent> hapticEventFromRaw(std::string_view raw);
std::string_view title(HapticEvent e);
std::string_view detail(HapticEvent e);

/// Optional second pulse after a delay: two-stage clicks, a spring-back on release.
struct HapticEcho {
    int ordinal{};
    int intensity{};
    int delayMs{};
    bool operator==(const HapticEcho&) const = default;
};

struct HapticRule {
    bool enabled{};
    int ordinal{};    // waveform ordinal in the device table
    int intensity{};  // 1...4
    std::optional<HapticEcho> echo;
    bool operator==(const HapticRule&) const = default;
};

/// `HapticEvent.defaults` from Settings.swift.
const HapticRule& defaultRule(HapticEvent e);

}  // namespace ixpat
