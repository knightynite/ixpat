#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/events.h"

namespace ixpat {

enum class ButtonGesture : std::uint8_t { press, doublePress, hold };
std::string_view rawName(ButtonGesture g);
std::optional<ButtonGesture> gestureFromRaw(std::string_view raw);
std::string_view title(ButtonGesture g);

/// What the action button (or a ring chamber, or a gesture) does. Same cases as Settings.swift's
/// `ButtonAction`; the payload cases carry Windows values (virtual-key codes, paths, PowerShell).
enum class ActionKind : std::uint8_t {
    none, actionRing, gestures, missionControl, spotlight, screenshot, lockScreen, playPause, showDesktop,
    volumeUp, volumeDown, mute, nextTrack, previousTrack, brightnessUp, brightnessDown, spaceLeft, spaceRight,
    launchApp, shortcut, shell
};
std::string_view rawName(ActionKind k);
std::optional<ActionKind> actionKindFromRaw(std::string_view raw);
std::string_view title(ActionKind k);

/// Modifier bits of `ButtonAction::flags` on Windows.
constexpr std::uint64_t kModCtrl = 1, kModAlt = 2, kModShift = 4, kModWin = 8;

struct ButtonAction {
    ActionKind kind = ActionKind::none;
    std::string path;      // launchApp
    int keyCode = -1;      // shortcut: virtual-key code
    std::uint64_t flags = 0;  // shortcut: kMod* bits
    std::string label;     // shortcut: human-readable ("Ctrl+C")
    std::string command;   // shell

    /// Stepped actions repeat every `stepPx` of gesture travel; the rest fire per swipe.
    bool isStepped() const;
    std::string describe() const;
    static ButtonAction blank(ActionKind k);
    bool operator==(const ButtonAction&) const = default;
};

/// One chamber of the Action Ring. `symbol` is an icon key (the Mac's SF Symbol name).
struct RingItem {
    std::string id;
    std::string title;
    std::string symbol;
    ButtonAction action;
    bool operator==(const RingItem&) const = default;
};
std::vector<RingItem> defaultRingItems();

/// Per-app overrides. Every optional is "use the base setting" when empty. `bundleID` holds the
/// lowercased process image name on Windows ("chrome.exe"); the key name is kept for the shared schema.
struct AppProfile {
    std::string id;
    std::string bundleID;
    std::string name;
    std::map<std::string, HapticRule> rules;  // keyed by raw HapticEvent name
    std::optional<bool> hapticsEnabled, smoothScroll, freeSpin, ratchetDynamic;
    std::optional<double> ratchetLightRate;
    std::optional<int> scrollStepEvery, freeSpinTextureEvery;
    std::optional<std::vector<RingItem>> ringItems;
    std::optional<ButtonAction> gestureUp, gestureDown, gestureLeft, gestureRight;
    bool operator==(const AppProfile&) const = default;
};

/// Random version-4 UUID, uppercase, as Swift's `UUID().uuidString` writes it.
std::string newUuidUpper();

}  // namespace ixpat
