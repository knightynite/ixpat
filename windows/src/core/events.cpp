#include "core/events.h"

#include <array>

namespace ixpat {
namespace {

struct Row {
    std::string_view raw;
    std::string_view title;
    std::string_view detail;
    HapticRule rule;
};

constexpr HapticRule R(bool on, int ordinal, int intensity) { return HapticRule{on, ordinal, intensity, std::nullopt}; }
constexpr HapticRule R(bool on, int ordinal, int intensity, HapticEcho echo) { return HapticRule{on, ordinal, intensity, echo}; }

// One row per enum value, in enum order. Copied from Settings.swift (title, detail, defaults).
// Ordinals: 3 HOVER 9 ms, 4 COLLIDE 26, 5 ALIGN 9, 6 STEP 11, 7 GROW 200, 8 SUCCESS 70, 9 ERROR 147, 10 CLICK 11.
constexpr std::array<Row, kHapticEventCount> kRows{{
    {"leftDown", "Left click", "From the mouse's own report. The firmware may already click on its own.", R(false, 10, 2)},
    {"leftUp", "Left release", "A lighter tick on release, like a mechanical switch.", R(false, 3, 1)},
    {"rightDown", "Right click", "From the mouse's own report. The firmware may already click on its own.", R(false, 10, 2)},
    {"rightUp", "Right release", "A lighter tick on release, like a mechanical switch.", R(false, 3, 1)},
    {"middleDown", "Wheel click", "Pressing the scroll wheel.", R(true, 10, 3)},
    {"scrollStep", "Scroll notch",
     "The ratchet detent. With Dynamic ratchet on, slow notches use this waveform and fast ones a lighter Step.",
     R(false, 6, 2)},
    {"screenEdge", "Pointer hits a screen edge", "Once each time the pointer is pushed against a display edge.",
     R(true, 4, 3)},
    {"dragEnd", "Drag & drop lands", "Mouse up after a drag of more than 20 px.", R(true, 5, 3)},
    {"windowSnap", "Window tiles", "The focused window's frame changed to a snap slot after a drag.", R(true, 8, 3)},
    {"appSwitch", "App switches", "Another app came to the foreground (Alt+Tab, taskbar, click).", R(true, 3, 2)},
    {"actionButton", "Action button", "Whenever the action button is recognised: a crisp tap and a lighter second one.", R(true, 10, 3, HapticEcho{10, 2, 65})},
    {"menuHighlight", "Menu item highlights",
     "A tick as the highlighted item changes while sweeping a menu or a right-click menu.", R(true, 6, 2)},
    {"textSelect", "Text selection, per word",
     "A soft tick as the selection gains or loses a word, paced so a fast drag purrs instead of buzzing.",
     R(true, 3, 1)},
    {"textLine", "Text selection crosses a line",
     "A slightly firmer notch each time the selection reaches another line.", R(true, 5, 2)},
    {"ringDetent", "Action Ring detent", "Crossing from one Action Ring chamber to the next.", R(true, 3, 2)},
    {"ringOpen", "Action Ring opens", "The ring appears under the pointer.", R(true, 7, 2)},
    {"ringCommit", "Action Ring runs an action", "A chamber was chosen.", R(true, 10, 4)},
    {"freeSpinOn", "Wheel enters free-spin", "A flick of the wheel switches it to free-spin.", R(true, 4, 3)},
    {"freeSpinOff", "Wheel re-engages the ratchet",
     "The wheel has come to rest after a free-spin and the notches are back. Off unless you want the hum.", R(false, 5, 2)},
    {"leftHold", "Left button held",
     "Plays once the button has stayed down for the hold time: a swell under a long press, a second stage to the click.",
     R(false, 7, 2)},
    {"rightHold", "Right button held",
     "Plays once the button has stayed down for the hold time: a swell under a long press, a second stage to the click.",
     R(false, 7, 2)},
    {"freeSpinTexture", "Free-spin texture",
     "A faint tick every few notches while the wheel free-spins, so a fast slide still has grain.", R(false, 3, 1)},
    {"gestureStep", "Gesture step (volume, brightness)",
     "Each volume or brightness step while the action button is held and the mouse moves up or down: tick, tick, tick.",
     R(true, 6, 2)},
    {"gestureLimit", "Gesture hits a limit", "Volume already at the top or bottom: a bump instead of a step.",
     R(true, 4, 3)},
    {"gestureSwipe", "Gesture swipe (track, desktop)",
     "A sideways gesture ran its action (next track rises, previous track falls).", R(true, 8, 3)},
    {"controlHover", "Pointer enters a control",
     "Buttons, tabs, taskbar icons, links and pop-ups as the pointer arrives on them. One tick per control.",
     R(true, 3, 1)},
    {"hoverAffordance", "Cursor changes shape",
     "Resize edges, text (I-beam), link hands: the cursor changed shape under the pointer. Can be busy; off by default.",
     R(false, 3, 1)},
    {"dropTarget", "Drag crosses a drop target",
     "While dragging, the cursor gains or loses its badge, or the element under it changes: a folder, a taskbar icon, a text field.",
     R(true, 3, 2)},
    {"sliderTick", "Slider detent", "A slider in the front app crossed one of 20 detents across its range.",
     R(true, 6, 1)},
    {"selectionChange", "Tab or item selected",
     "A tab, segment, slide thumbnail or list selection changed under the mouse.", R(true, 5, 2)},
    {"scrollEnd", "Scrolling hits the end",
     "The scroll bar of the front app reached the top or bottom: an overscroll bump.", R(true, 4, 2)},
    {"pluginPulse", "App plugin pulse",
     "Pulses requested by app plugins over the local bridge (Diagnostics tab). The plugin picks the waveform; this switch gates it.",
     R(true, 10, 3)},
    {"forceClick", "Force click",
     "The second, deeper click of a press-and-hold, the moment the hold action runs (Action Button tab).",
     R(true, 10, 4)},
}};

constexpr const Row& row(HapticEvent e) { return kRows[static_cast<std::size_t>(e)]; }

}  // namespace

std::string_view rawName(HapticEvent e) { return row(e).raw; }
std::string_view title(HapticEvent e) { return row(e).title; }
std::string_view detail(HapticEvent e) { return row(e).detail; }
const HapticRule& defaultRule(HapticEvent e) { return row(e).rule; }

std::optional<HapticEvent> hapticEventFromRaw(std::string_view raw) {
    for (std::size_t i = 0; i < kRows.size(); ++i) {
        if (kRows[i].raw == raw) return static_cast<HapticEvent>(i);
    }
    return std::nullopt;
}

}  // namespace ixpat
