# iXpat for Windows: plan

Goal: give the Surface Mouse 2nd Edition on Windows the same feature set iXpat gives it
on macOS. Windows ships the haptics for a handful of system gestures and leaves the rest
(menu ticks, selection ticks, dynamic ratchet, kinetic scroll, action-button remapping,
per-app profiles, a plugin bridge) to the firmware defaults. Everything below is scoped
so the two apps share one settings schema, one haptic-event vocabulary and one bridge API.

## 1. What the mouse gives us (measured on macOS, applies to Windows)

| Collection | Use |
|---|---|
| Mouse (0x01/0x02), report 0x03 | buttons, X/Y, wheel, AC pan |
| Keyboard (0x01/0x06), report 0x01 | the action button's chords: Win+Shift+S, Win + fake left click, Win+Shift+F23 held |
| Consumer (0x0C/0x01) | media keys |
| Simple Haptic Controller (0x0E/0x01) | output 0x43 = [ordinal 3…10, intensity 0…4]; feature 0x42 = waveform table (HOVER, COLLIDE, ALIGN, STEP, GROW, SUCCESS, ERROR, CLICK) |
| 0xFF07/0x92 vendor | Mouse and Keyboard Center command channel (feature 0x24 / input 0x27, 128 B); protocol unknown |
| 0xFF0B/0x104 vendor | 60 B buffered report 0x2A in/out/feature, 0x2C, 0x2D; protocol unknown |

Firmware seen: 5.29.139. Full descriptor notes live in the research folder (`NOTES.md`).

## 2. Windows building blocks

**Input (per-device).** Raw Input (`RegisterRawInputDevices` + `WM_INPUT`) reports every
event with the device handle, so the app can tell the Surface Mouse from the trackpad
or another mouse. Raw Input cannot block or modify events, so shaping needs a low-level
mouse hook (`WH_MOUSE_LL`) as well. The pairing rule: the hook decides, Raw Input tells
it which device the event came from (correlate by arrival order and delta; this is the
same trick MKC-style tools use). Synthetic scrolls and clicks go out through `SendInput`
tagged with `dwExtraInfo` so the hook ignores its own events (same as the `ownTag`
on macOS).

**Haptics.** Open the haptics top-level collection with `CreateFile` on the HID
interface path (`SetupDi` / `CM_Get_Device_Interface_List` with `GUID_DEVINTERFACE_HID`,
filter on VID 045E PID 0880 and usage page 0x0E) and send output report 0x43 with
`HidD_SetOutputReport` or `WriteFile`. Windows keeps the mouse and keyboard collections
exclusive to the kernel, but vendor and haptics collections are openable from user
mode. First thing to measure: whether Windows 11's own haptic-mouse support holds the
collection open; if it does, fall back to the vendor channel once its protocol is known.

**Menu and selection ticks.** UI Automation replaces the macOS Accessibility API:
`IUIAutomation::ElementFromPoint` under the pointer for menu-item highlights (Chromium
and WinUI both expose menus through UIA), `TextPattern` selection ranges for word and
line ticks, `AddFocusChangedEventHandler` and property-changed handlers for slider and
tab ticks, `WindowPattern` plus `GetWindowRect` deltas for snap detection. Run UIA on a
worker thread with a COM MTA, never on the hook thread (hook callbacks must return in
well under the `LowLevelHooksTimeout`).

**Action button.** The chords arrive as keyboard input. A low-level keyboard hook
(`WH_KEYBOARD_LL`) recognises the three sequences, swallows them, and runs the mapped
action; Raw Input confirms the keystrokes came from the mouse's keyboard collection,
so a real Win+Shift+S from the keyboard still opens Snipping Tool.

**Pointer settings.** `SystemParametersInfo(SPI_SETMOUSESPEED)` is global; per-device
speed needs the vendor channel (what Mouse and Keyboard Center does). Until that
protocol is captured, apply the speed only while the Surface Mouse is the active
device (Raw Input tells us) and restore it when another device moves the pointer.

**Force click.** Windows has no system-wide force-click concept. Offer the same hold
gesture with configurable actions (preview via the app's own Quick Look equivalent is
not available; sensible defaults are "show context menu", "middle click" or a plugin
pulse) rather than pretending to be a trackpad.

**Gestures, Action Ring, per-app profiles, feel presets, bridge.** Straight ports: the
pointer freeze uses `ClipCursor` plus discarding hook deltas, the ring is a layered
topmost window, per-app profiles key on the foreground process image name
(`GetForegroundWindow` → `QueryFullProcessImageName`), the bridge keeps the same
loopback HTTP API and port (47337) so the Photoshop plugin works unchanged.

## 3. Architecture

- **Core service** (`ixpat-core`): Rust with the `windows` crate, or C++20 with Win32.
  Rust is the recommendation: the hook, Raw Input, HID and UIA code is all unsafe FFI
  either way, and Rust makes the cross-thread state (the `Locked<T>` pattern) hard to get
  wrong. Runs as a tray app; no service and no driver needed.
- **UI**: WinUI 3 (C# or C++/WinRT) talking to the core over a named pipe, or egui in
  the same process if we want a single binary. Mirror the macOS tabs: Haptics, Action
  Button, Action Ring, Apps, Pointer & Scroll, Diagnostics.
- **Shared schema**: the macOS `settings.json` (haptic event names, rules, profiles,
  ring items, scroll parameters) is the contract. Add a `platform` section for the few
  Windows-only knobs rather than forking the file.
- **Signing and install**: MSIX or a signed MSI; the hooks do not need elevation, but
  interacting with elevated windows needs a UIAccess-signed binary in Program Files.

## 4. Milestones

1. **Probe week.** A command-line tool that enumerates the mouse's HID collections,
   plays the eight waveforms, and logs Raw Input + hook events with device handles.
   Measures the two open questions: can user mode open the haptics collection while
   Windows is using it, and how reliably Raw Input and the hook can be correlated.
2. **Core input and haptics.** Hook + Raw Input + SendInput; click/wheel/hold rules;
   dynamic ratchet, free-spin and kinetic scroll ported from `EventMonitor.swift`.
3. **Ticks.** UIA menu, selection, slider, tab and snap watchers.
4. **Action button, gestures, ring, profiles, bridge.**
5. **UI and installer.**
6. **Vendor channel capture.** With the tool running on Windows, record what Mouse and
   Keyboard Center sends on 0xFF07 when it changes the action button or pointer speed.
   That unlocks per-device pointer settings on both platforms.

## 5. Firmware updates

**What Microsoft ships today (measured 2026-10-17).** The only published package,
"Surface Mouse firmware v2.10" (version 1.2.13, 24 March 2026, Download Center
id 108581), is an MSI holding an INF plus four files: two 16-byte *offer* files and two
signed *payload* files (`SurfaceMouse_MCU_Hi`, `SurfaceMouse_MCU_Lo`). The INF binds
Windows' inbox **HidCfu** driver to `HID\{00001812-…}_Dev_VID&02045e_PID&0854&Col04`
with the Component Firmware Update usages (offer in 0x8A / out 0x8E, payload in 0x66 /
out 0x61, versions feature 0x62). That is Microsoft's open CFU protocol, with a public
specification and reference host implementation, applied over Bluetooth LE HID; the
"re-pair and keep still for 10 minutes" instruction is the background CFU transfer.

**It does not apply to the 2nd Edition.** PID 0854 is the original Surface Mouse. The
2nd Edition is PID 0880, shipped on 13 October 2026, has no published firmware package
yet, and its report descriptor (as read on macOS) exposes no CFU collection: the only
candidates are the 0xFF07 and 0xFF0B vendor collections, whose protocols are unknown.

**Plan.**
1. Watch Windows Update and the Download Center for the first 2nd Edition package.
   The INF will name the collection and usages; if it includes HidCfu, the protocol is
   CFU and the reference host code can be ported to both apps.
2. On the first update, capture it on Windows with the probe tool (Raw Input and HID
   logging) and an ETW HID trace, so the on-the-wire sequence is known rather than
   inferred from the spec. Record it under coordinated-disclosure-grade notes: offers,
   responses, chunk size, timing, battery and version checks.
3. Implement an updater in both apps that only installs Microsoft's signed packages,
   reads the installed version first, refuses on low battery, checks the offer response
   before sending any payload, and never writes on the vendor channels without a
   captured reference. The macOS side waits until the Windows side has completed one
   verified update.
4. Until then, the apps show the installed firmware version and point the user at the
   official update path (Surface app → Help & support → Check for updates).

Sources: Download Center page for Surface Mouse firmware v2.10, its INF
(`SurfaceMouseFwUpdate.inx`), Microsoft's Surface accessories lifecycle page, and the
report descriptor notes in the research folder.
