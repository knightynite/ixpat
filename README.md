# Thrum

Haptics, an Action Ring, kinetic scrolling and per-mouse pointer settings for the
**Microsoft Surface Mouse (2nd Edition)** on macOS. A menu-bar app, in the same
family as Knurl (the Magic Trackpad one).

The mouse exposes a standard HID *Simple Haptic Controller* (usage page 0x0E),
the same interface the Surface app drives on Windows. macOS has no driver for it,
but user-space IOKit can read the waveform table and fire triggers with no kernel
extension and no private framework. Thrum does that and layers on the parts
Windows offers, plus a few Logitech MX-style ones.

## What it does

- **Live schematic**: buttons, wheel and action button light up from the mouse's
  own HID reports; every haptic pulse ripples out of the body.
- **Haptics**: a waveform (Click, Hover, Collide, Align, Step, Grow, Success,
  Error, read from the device) and a strength for each event: clicks, wheel
  click, scroll detents, screen edges, drag & drop, window tiling, app switches,
  the action button, menu highlights (menu bar and right-click menus), text
  selection by word and by line, Action Ring detents, free-spin on and off.
- **Dynamic ratchet**: firm detents when scrolling slowly, light quick ticks when
  fast, a firmer bump on reversal, silence in free-spin and a soft pulse when the
  ratchet re-engages.
- **Kinetic scrolling**: each notch is a velocity impulse with friction that
  follows the wheel speed. A slow notch settles crisply at exactly the set
  distance; a flick accelerates and coasts, Magic Mouse / Windows style.
  Momentum and acceleration are sliders.
- **Action button**: the firmware sends keyboard chords (press = Win+Shift+S,
  double = Win plus a synthetic left click, hold = Win+Shift+F23). Thrum
  decodes them, hides them from apps, and maps each gesture to an action:
  Action Ring, Mission Control, Spotlight, screenshot, lock, play/pause,
  show desktop, open an app, a shortcut, a shell command.
- **Action Ring**: an MX Master 4-style radial menu under the pointer with up to
  eight editable chambers and a haptic detent per chamber.
- **Pointer & scroll**: tracking speed, no-acceleration, scroll speed and
  reversed scrolling for *this mouse only*; the trackpad is untouched.
- **Feel presets**: named click feels for each button (Subtle, Crisp, Mechanical,
  Deep, Push back, Double tap, Hold & swell, …) and wheel feels (Classic ratchet,
  Fine, Mechanical, Heavy, Coarse, Silk, Slider, Rumble, Silent). A rule can carry a
  second pulse (echo) after a delay; a button held past the hold time plays a swell;
  a free-spinning wheel can keep a faint texture.
- **Hold-and-move gestures**: keep the action button down and move the mouse (the
  pointer stays put, MX gesture-button style). Up/down step the volume or brightness
  with a tick per step and a bump at the end of the range; left/right swipe to the
  previous/next track (or desktops, or any shortcut). Release without moving and the
  hold's own action runs as usual.
- **Diagnostics**: permissions, device identity, haptic table, raw report log.

## Build and run

    ./build.sh          # release build → Thrum.app
    open Thrum.app

Needs Xcode 16+ / macOS 14+. The first launch opens the settings window
(set `THRUM_NO_WINDOW=1` to suppress it). `build.sh` signs with a local
codesigning identity when one is present so TCC grants survive rebuilds;
set `THRUM_SIGN_ID` to pick one, otherwise it falls back to ad-hoc signing.

## Permissions

| Permission | Needed for |
|---|---|
| none | haptic playback, test buttons, waveform table, pointer settings |
| Input Monitoring | reading the mouse's reports: click, wheel and action-button events, screen-edge and drag detection |
| Accessibility | kinetic / reversed scrolling, hiding the button's chord, menu and text-selection ticks, window-tiling detection, firing shortcuts |

Pointer settings go through Apple's `hidutil` and need no permission.

## How the tricky parts work

- The CGEvent tap and the 120 Hz scroll timer run on a dedicated
  user-interactive thread (`TapThread`), never on main, so nothing in the UI can
  delay pointer events.
- Accessibility polling runs on a utility queue. Right-click menus in Chromium
  apps are not in the app's accessibility children and never become the focused
  element; the only reliable way to find the highlighted item is to hit-test the
  element under the pointer (`AXUIElementCopyElementAtPosition`).
- Haptic triggers go out as output report 0x43 `[ordinal, intensity 0–4]` on a
  serial IOKit queue. The device plays one waveform at a time, so the engine
  rate-limits by each waveform's reported duration.

## Files

    Sources/Thrum/App          app delegate, wiring, debug log
    Sources/Thrum/Device       IOHIDManager device, reports, haptic table
    Sources/Thrum/Haptics      rule engine
    Sources/Thrum/Input        event tap, tap thread, action button, menu watcher, hidutil
    Sources/Thrum/Window       Action Ring panel
    Sources/Thrum/UI           settings window, schematic, menu, theme
    Sources/Thrum/Store        settings model and persistence
    tools/make_icon.swift      renders the app icon

Settings live in `~/Library/Application Support/Thrum/settings.json`; the debug
log and raw report log sit next to it.

## Measured facts (fw 5.29)

Output report 0x43 = `[ordinal, intensity 0–4]`; feature 0x42 = 50 durations
then 50 waveform usages; ordinals 3–10 are Hover (9 ms), Collide (26), Align (9),
Step (11), Grow (200), Success (70), Error (147), Click (11). Two vendor channels
(pages 0xFF07 and 0xFF0B) exist and are not touched; their protocol is unknown
until captured on Windows.
