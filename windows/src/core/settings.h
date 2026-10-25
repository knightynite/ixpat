#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/actions.h"
#include "core/events.h"

namespace ixpat {

enum class AppTheme : std::uint8_t { light, dark, system };
std::string_view rawName(AppTheme t);
std::optional<AppTheme> themeFromRaw(std::string_view raw);

/// What a press-and-hold of the left button does on Windows (the Mac's Force Click has no equivalent).
enum class HoldAction : std::uint8_t { none, contextMenu, middleClick, actionRing };
std::string_view rawName(HoldAction a);
std::optional<HoldAction> holdActionFromRaw(std::string_view raw);

/// Windows-only knobs, stored under "windows" in settings.json.
struct WindowsSettings {
    HoldAction holdAction = HoldAction::contextMenu;
    bool startAtLogin = false;
    bool showDiagnostics = false;  // the Diagnostics page is for finding faults, not for every day
    bool operator==(const WindowsSettings&) const = default;
};

/// The whole settings model: Settings.swift's published properties with the same defaults.
struct Settings {
    // Master
    bool enabled = true;
    bool hapticsEnabled = true;
    int masterIntensity = 4;
    std::array<HapticRule, kHapticEventCount> rules{};
    int scrollStepEvery = 1;
    // Pointer & scroll (this mouse only)
    bool managePointer = false;
    double pointerSpeed = 3.0;
    bool noAcceleration = false;
    double scrollSpeed = 0.3125;
    bool reverseScroll = false;
    // Wheel feel
    bool smoothScroll = true;
    double scrollPixelsPerNotch = 60;
    double scrollGlide = 5;
    double scrollMomentum = 4;
    double scrollAccel = 1;
    bool freeSpin = true;
    bool ratchetDynamic = true;
    double ratchetLightRate = 10;
    double freeSpinThreshold = 18;
    double freeSpinMultiplier = 3;
    // Action Ring
    std::vector<RingItem> ringItems;
    // Action button: indexed by ButtonGesture
    std::array<ButtonAction, 3> actions{};
    // Hold-and-move gestures
    ButtonAction gestureUp, gestureDown, gestureLeft, gestureRight;
    double gestureStepPx = 24;
    double gestureSwipePx = 90;
    bool gestureFreezePointer = true;
    bool gesturesOnHold = true;
    // Per-app profiles
    std::vector<AppProfile> profiles;
    // Plugin bridge
    bool bridgeEnabled = true;
    int bridgePort = 47337;
    std::string bridgeToken;
    bool autoCheckUpdates = true;
    // Click feel extras
    int clickHoldMs = 300;
    bool forceClick = true;
    int forceClickMs = 350;
    int freeSpinTextureEvery = 2;
    bool swallowButton = true;
    // Appearance
    AppTheme theme = AppTheme::system;
    std::uint32_t accentHex = 0;  // 0 = Windows' own accent colour
    // Windows-only
    WindowsSettings windows;

    // Not persisted: the process image name of the foreground app ("chrome.exe"), lowercased.
    std::optional<std::string> frontBundleID;

    const HapticRule& baseRule(HapticEvent e) const { return rules[static_cast<std::size_t>(e)]; }
    HapticRule& baseRule(HapticEvent e) { return rules[static_cast<std::size_t>(e)]; }
    /// The rule in force: the front app's profile override, else the base rule.
    const HapticRule& rule(HapticEvent e) const;
    const ButtonAction& action(ButtonGesture g) const { return actions[static_cast<std::size_t>(g)]; }
    ButtonAction& action(ButtonGesture g) { return actions[static_cast<std::size_t>(g)]; }

    /// The front app's profile, if it has one.
    const AppProfile* profile() const;
    bool effHapticsEnabled() const;
    bool effSmoothScroll() const;
    bool effFreeSpin() const;
    bool effRatchetDynamic() const;
    double effRatchetLightRate() const;
    int effScrollStepEvery() const;
    int effFreeSpinTextureEvery() const;
    const std::vector<RingItem>& effRingItems() const;
    const ButtonAction& effGestureUp() const;
    const ButtonAction& effGestureDown() const;
    const ButtonAction& effGestureLeft() const;
    const ButtonAction& effGestureRight() const;
};

Settings defaultSettings();
std::string newBridgeToken();  // 24 chars of [a-z0-9]

/// Keep every number inside the range the UI offers (Settings.swift `sanitize`).
void sanitize(Settings& s);

/// Pretty-printed, sorted keys, with "platform":"windows" and the "windows" section.
std::string toJson(const Settings& s);

struct ParseError {
    std::string message;
};
/// Missing keys take defaults, unknown keys are ignored, bad values fall back; then sanitize().
std::variant<Settings, ParseError> parseSettings(std::string_view json);

}  // namespace ixpat
