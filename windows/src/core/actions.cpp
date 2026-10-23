#include "core/actions.h"

#include <array>
#include <cstdio>
#include <random>

namespace ixpat {
namespace {

constexpr std::array<std::string_view, 3> kGestureRaw{"press", "doublePress", "hold"};
constexpr std::array<std::string_view, 3> kGestureTitle{"Press", "Double press", "Press and hold"};

struct KindRow {
    std::string_view raw;
    std::string_view title;
};
constexpr std::array<KindRow, 21> kKinds{{
    {"none", "Do nothing"},
    {"actionRing", "Action Ring"},
    {"gestures", "Hold-and-move gestures"},
    {"missionControl", "Task View"},
    {"spotlight", "Search"},
    {"screenshot", "Snipping Tool"},
    {"lockScreen", "Lock screen"},
    {"playPause", "Play / Pause"},
    {"showDesktop", "Show desktop"},
    {"volumeUp", "Volume up (step)"},
    {"volumeDown", "Volume down (step)"},
    {"mute", "Mute"},
    {"nextTrack", "Next track"},
    {"previousTrack", "Previous track"},
    {"brightnessUp", "Brightness up (step)"},
    {"brightnessDown", "Brightness down (step)"},
    {"spaceLeft", "Previous desktop"},
    {"spaceRight", "Next desktop"},
    {"launchApp", "Open an app..."},
    {"shortcut", "Keyboard shortcut..."},
    {"shell", "Shell command..."},
}};

std::string fileStem(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name.erase(dot);
    return name;
}

}  // namespace

std::string_view rawName(ButtonGesture g) { return kGestureRaw[static_cast<std::size_t>(g)]; }
std::string_view title(ButtonGesture g) { return kGestureTitle[static_cast<std::size_t>(g)]; }
std::optional<ButtonGesture> gestureFromRaw(std::string_view raw) {
    for (std::size_t i = 0; i < kGestureRaw.size(); ++i) {
        if (kGestureRaw[i] == raw) return static_cast<ButtonGesture>(i);
    }
    return std::nullopt;
}

std::string_view rawName(ActionKind k) { return kKinds[static_cast<std::size_t>(k)].raw; }
std::string_view title(ActionKind k) { return kKinds[static_cast<std::size_t>(k)].title; }
std::optional<ActionKind> actionKindFromRaw(std::string_view raw) {
    for (std::size_t i = 0; i < kKinds.size(); ++i) {
        if (kKinds[i].raw == raw) return static_cast<ActionKind>(i);
    }
    return std::nullopt;
}

bool ButtonAction::isStepped() const {
    switch (kind) {
        case ActionKind::volumeUp:
        case ActionKind::volumeDown:
        case ActionKind::brightnessUp:
        case ActionKind::brightnessDown:
            return true;
        default:
            return false;
    }
}

std::string ButtonAction::describe() const {
    switch (kind) {
        case ActionKind::launchApp:
            return path.empty() ? "Open an app (none chosen)" : "Open " + fileStem(path);
        case ActionKind::shortcut:
            return label.empty() ? "Keyboard shortcut (not recorded)" : "Press " + label;
        case ActionKind::shell:
            return command.empty() ? "Shell command (empty)" : command;
        default:
            return std::string(title(kind));
    }
}

ButtonAction ButtonAction::blank(ActionKind k) {
    ButtonAction a;
    a.kind = k;
    return a;
}

std::vector<RingItem> defaultRingItems() {
    auto shortcut = [](int vk, std::uint64_t flags, const char* label) {
        ButtonAction a = ButtonAction::blank(ActionKind::shortcut);
        a.keyCode = vk;
        a.flags = flags;
        a.label = label;
        return a;
    };
    return {
        {newUuidUpper(), "Task View", "rectangle.3.group", ButtonAction::blank(ActionKind::missionControl)},
        {newUuidUpper(), "Search", "magnifyingglass", ButtonAction::blank(ActionKind::spotlight)},
        {newUuidUpper(), "Screenshot", "camera.viewfinder", ButtonAction::blank(ActionKind::screenshot)},
        {newUuidUpper(), "Play / Pause", "playpause.fill", ButtonAction::blank(ActionKind::playPause)},
        {newUuidUpper(), "Show Desktop", "menubar.dock.rectangle", ButtonAction::blank(ActionKind::showDesktop)},
        {newUuidUpper(), "Lock", "lock.fill", ButtonAction::blank(ActionKind::lockScreen)},
        {newUuidUpper(), "Copy", "doc.on.doc", shortcut(0x43, kModCtrl, "Ctrl+C")},
        {newUuidUpper(), "Paste", "doc.on.clipboard", shortcut(0x56, kModCtrl, "Ctrl+V")},
    };
}

std::string newUuidUpper() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t hi = rng(), lo = rng();
    hi = (hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;  // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10xx
    char buf[37];
    std::snprintf(buf, sizeof buf, "%08llX-%04llX-%04llX-%04llX-%012llX",
                  static_cast<unsigned long long>(hi >> 32), static_cast<unsigned long long>((hi >> 16) & 0xFFFF),
                  static_cast<unsigned long long>(hi & 0xFFFF), static_cast<unsigned long long>(lo >> 48),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return buf;
}

}  // namespace ixpat
