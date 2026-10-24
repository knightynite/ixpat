# iXpat for Windows: design

Date: 2026-10-22. Status: approved direction (C++20, Win32 + Direct2D, single binary). This is the spec the
implementation plan in `plan.md` is built from.

## 1. Goal and success criteria

Give the Microsoft Surface Mouse (2nd Edition) on Windows 10/11 the same feature set the macOS app gives it,
from one small native binary that needs no driver, no service, no elevation and no runtime.

Success means:

- Every macOS feature that has a Windows analogue works, with the same names, defaults and feel: haptic
  rules and feel presets, dynamic ratchet and free-spin, kinetic scrolling, action button remapping,
  hold-and-move gestures, Action Ring, per-app profiles, menu/control/selection ticks, plugin bridge,
  diagnostics, update check.
- The input hot path adds no perceptible latency: hook callbacks return in microseconds and never block on
  I/O, COM or the UI.
- One `iXpat.exe` per architecture (x64, ARM64), under 5 MB, idle under 15 MB of RAM, instant start.
- `settings.json` uses the macOS schema; a plugin written for the Mac bridge works unchanged.
- Shipped: GitHub release assets, product page section, README, corrected Windows notes.

Out of scope: a kernel driver, firmware flashing, the 0xFF07 Mouse and Keyboard Center channel (unknown
protocol; left untouched), Force Click as macOS implements it (Windows has no pressure events).

## 2. What the device gives us on Windows (measured 2026-10-22 on this PC, fw 5.29)

| Collection | Usage | Windows binding | Use |
|---|---|---|---|
| Col01 | 0x01/0x02 mouse | mouhid | buttons, X/Y, wheel: only via Raw Input |
| Col02 | 0x01/0x06 keyboard | kbdhid | action-button chords: only via Raw Input |
| Col03 | 0x0C/0x01 consumer | hidserv | media keys |
| Col04 | 0xFF07/0x92 vendor | none | Mouse and Keyboard Center channel, protocol unknown, never written |
| Col05 | 0xFF0B/0x104 vendor | **HidCfu** (UMDF, `SurfaceMouse2ndEditionFwUpdate.inf` 1.0.5.0) | Microsoft CFU firmware update: offer in 0x8A / out 0x8E, payload in 0x66 / out 0x61, versions feature 0x62; components MCU1, MCU2, HAPTIC. Read-only for us (versions), never written |
| Col06 | 0x0E/0x01 Simple Haptic Controller | none (HID class only) | output 0x43 `[ordinal, intensity 0-4]`, feature 0x42 waveform table |

The macOS plan's statements that the 2nd Edition has no firmware package and that 0xFF0B is unknown are
both wrong and get corrected in `docs/windows-plan.md`.

## 3. Architecture

Single process, six threads, one language. No DLLs of our own, no IPC.

```
  UI thread (main)                                  Input thread (TIME_CRITICAL)
  tray, windows, settings model, Router             WH_MOUSE_LL, WH_KEYBOARD_LL
  hold timers, free-spin timer, foreground hook     Raw Input (WM_INPUT), 120 Hz coast timer
  haptic rule resolution -> HID queue               device attribution, gesture accumulator
        ^ PostMessage(events)      ConfigSnapshot v  drag / edge detection, SendInput (tagged)

  HID thread            UIA thread (MTA)          Bridge thread        Updater thread
  open 0x0E, read 0x42  ElementFromPoint          Winsock loopback     WinHTTP, GitHub API
  serial 0x43 writes    TextPattern, RangeValue   HTTP/1.1 parser      version compare
  arrival/removal       Scroll, SelectionItem     token gate
```

Rule of the house: anything that is not pure Win32 glue lives in `core/` as plain C++20 with no Windows
headers, and is unit-tested. Win32 code in `win/` is thin and does one thing per file.

### 3.1 Source layout (monorepo, next to the Mac sources)

```
windows/
  CMakeLists.txt, CMakePresets.json       Ninja + MSVC; presets x64-debug, x64-release, arm64-release
  src/core/      settings model + JSON, rules/profiles/presets, Router (rule routing from device events),
                 WheelFeel (ratchet, free-spin), Kinetic (coast integrator), ChordDecoder (action button),
                 GestureAccumulator, ring geometry, HapticTable parser, version compare, bridge protocol
  src/win/       hid_device, raw_input, hooks, input_thread, send_input, uia_watch, foreground, snap,
                 cursor_shape, tray, pointer_settings, bridge_server, updater, single_instance, log,
                 startup (Run key), dpi, timers, com
  src/ui/        d2d device/context, theme (the Fluent tokens), controls, navigation, settings window,
                 schematic, ring window, setup guide, about
  src/app/       main.cpp: wiring (the macOS AppDelegate), version.h
  tests/         doctest, one file per core unit, plus a Mac-written settings.json fixture
  tools/probe/   ixpat-probe.exe: enumerate, open, read table, play, log Raw Input + hook ordering
  third_party/   nlohmann/json.hpp (3.12.0), doctest.h (2.5.3); header-only, vendored, pinned
  installer/     Inno Setup script (per-user, no elevation), zip packaging script
  docs/          this design, the plan, measurement notes
.github/workflows/windows.yml             build x64 + ARM64, run tests, package on tag win-v*
```

### 3.2 Input: Raw Input is the device side, the hooks are the system side

Exactly the split the Mac app has (IOHID reports vs. the CGEvent tap):

- **Raw Input** (`RegisterRawInputDevices`, `RIDEV_INPUTSINK`, mouse 0x01/0x02, keyboard 0x01/0x06 and
  consumer 0x0C/0x01) on a message-only window owned by the input thread. `RAWINPUT.header.hDevice` is
  resolved once per handle to a device path; handles whose path contains `VID&02045E_PID&0880` (or
  `VID_045E&PID_0880` over USB) are the Surface Mouse. Raw Input drives everything the Mac drives from
  reports: button haptics, wheel notches for the ratchet, the chord decoder, the live schematic.
- **Low-level hooks** (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`) on the same thread decide pass / block / replace
  for the handful of events we shape: wheel notches (kinetic scroll, reverse), the chord keys and the
  firmware's fake left click, pointer motion in gesture mode, clicks while the ring is open, the hold
  action's release.
- **Attribution.** Inside the hook callback we first drain pending `WM_INPUT` messages
  (`PeekMessage(..., WM_INPUT, WM_INPUT, PM_REMOVE)`) into a small FIFO of (device, kind, content).
  The hook event is matched on **content** (wheel delta, button flag, key scan code), never on kind
  alone, because injected input showed the hook can fire before its `WM_INPUT` and a kind-only match
  pairs event N with event N-1 (see `measurements-2026-10.md`). A raw entry that arrives after its hook
  event is matched retroactively for the device-side state (schematic, rules); a hook event that must
  be decided before its raw entry arrives uses the Mac's rule: a non-injected wheel notch is shaped
  while the Surface Mouse is connected and no other pointing device sent a wheel report in the last
  2 s, and the chord keys are swallowed while the chord decoder reports `chordActive`. The probe tool
  measures the real device's ordering and the fallback rate.
- **Our own events** go out through `SendInput` with `dwExtraInfo = 0x49585054` ("IXPT"); the hooks let
  anything carrying that tag pass untouched (the Mac's `ownTag`).
- **Hook hygiene.** Hook procedures are `noexcept`, allocate nothing, take one uncontended lock at most,
  and post work to other threads. Windows removes a hook whose callback exceeds `LowLevelHooksTimeout`
  (300 ms default); the input thread runs at `THREAD_PRIORITY_TIME_CRITICAL` and does nothing else.
  A watchdog re-installs the hooks if the heartbeat counter stops advancing while input is arriving
  (checked every 5 s from main).

### 3.3 Haptics

- Enumerate `GUID_DEVINTERFACE_HID` interfaces (`CM_Get_Device_Interface_List`), open each with
  `CreateFile(GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE)`, keep the one whose
  `HIDD_ATTRIBUTES` are VID 0x045E / PID 0x0880 and whose `HIDP_CAPS.UsagePage` is 0x0E. Firmware
  version is `HIDD_ATTRIBUTES.VersionNumber` (0x0529 -> "5.29").
- Waveform table: `HidD_GetFeature` report 0x42 into a buffer of `FeatureReportByteLength`; parse 50 u16
  durations then 50 u16 usages into the ordinal 3.. list exactly as `MouseDevice.readHapticTable` does.
  The measured table is the fallback until the device answers.
- Trigger: `HidD_SetOutputReport` with `[0x43, ordinal, intensity]` padded to `OutputReportByteLength`
  (the CFU INF's `UseHidSetOutputReport=1` says SetOutputReport is the path this BLE stack supports;
  `WriteFile` is the fallback the probe also measures). One serial HID thread with a bounded queue (drop
  the oldest when more than 8 are pending); the engine rate-limits by each waveform's duration (min 35 ms)
  as `HapticEngine.play` does.
- Arrival and removal via `CM_Register_Notification` for the HID interface class; the mouse sleeps after
  idle so reconnects are routine and silent.
- Battery: the BLE device's battery property (`{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2`) read every
  15 min; firmware component versions via a read-only CFU `GET_FEATURE` 0x62 on Col05 when it opens,
  shown in Diagnostics. **Nothing is ever written to Col04 or Col05.**

### 3.4 Core logic ported from the Mac (pure C++, tested)

- `Settings` mirrors `Settings.swift`: same fields, same defaults, same clamps (`sanitize`), same JSON
  layout (pretty-printed, sorted keys, `rules` and `actions` keyed by raw names, unknown keys dropped,
  the older array layout of `RawMap` accepted). Atomic write (temp + `MoveFileEx(REPLACE_EXISTING)`),
  300 ms debounce, `settings.broken.json` kept on decode failure.
- Platform-local values keep their keys and change meaning: `bundleID` holds the lowercased process image
  name (`chrome.exe`); `shortcut.keyCode` is a Windows virtual-key code and `shortcut.flags` a bitmask
  (1 Ctrl, 2 Alt, 4 Shift, 8 Win); `launchApp.path` and `shell.command` are Windows paths and
  PowerShell commands. A top-level `"platform": "windows"` marks the file. Windows-only knobs live under
  `"windows": {"holdAction": "contextMenu", "startAtLogin": false}`.
- `Router` holds what `AppDelegate` does on main: button edges -> rules and hold timers; wheel notches ->
  `WheelFeel` (dynamic ratchet with the reversal bump, light ticks capped at 25/s, free-spin on/off with
  the 300 ms rest timer, texture every N) -> engine plays; action-button gestures -> actions, gesture
  mode, pending hold action; app switch -> profile and appSwitch pulse. It takes a clock and an output
  sink so tests can drive it.
- `Kinetic` is `EventMonitor.kineticNotch` + `scrollTick`: velocity impulses, friction time constant
  following wheel rate, exact per-frame integral, sub-pixel carry, reversal resets, 40 px/s stop. The
  Windows side feeds notches (a 120 wheel delta = one notch, capped at 2 per event) and emits pixel
  deltas which the input thread posts as `MOUSEEVENTF_WHEEL` / `MOUSEEVENTF_HWHEEL` with
  `WHEEL_DELTA`-scaled values (pixels * 120 / pxPerNotch, remainder carried) so apps that only do line
  scrolling still scroll correctly, while apps with smooth scrolling (browsers, Office) get the finer
  stream.
- `ChordDecoder` is `ActionButtonController.handle`: keyboard reports with Win+Shift+S -> press,
  Win+Shift+F23 held -> hold (release ends it), Win + a left click -> double press (or the Win release
  alone). Raw Input keyboard data arrives as make/break codes, so the decoder is fed (modifier state,
  key down/up) tuples and the test fixtures are the measured chord sequences.
- `GestureAccumulator` is `GestureController.handleMove` verbatim (10 px axis lock, step/swipe units,
  at most 6 per move).
- Ring geometry (`slotIndex`, dead zone 26 px, 8 chambers) from `RadialMenuController`.
- `BridgeProtocol`: request parsing, routing (`GET /ping`, `GET /waveforms`, `POST /haptic`), token check,
  intensity clamp 1..4, name validation, the same JSON bodies and CORS headers as `Bridge.swift`.
- Feel presets (`ClickFeel`, `WheelFeel`) and `clickFeelName` / `wheelFeelName` matching.

### 3.5 Windows equivalents of the macOS system hooks

| macOS | Windows |
|---|---|
| CGEvent tap on TapThread | WH_MOUSE_LL + WH_KEYBOARD_LL on the input thread |
| IOHID input reports | Raw Input |
| `CGAssociateMouseAndMouseCursorPosition(0)` (gesture freeze) | hook blocks the mouse's moves while the hold is active and re-centres with `SetCursorPos` if a move slipped through |
| `AXUIElementCopyElementAtPosition` menu / control hit-test | `IUIAutomation::ElementFromPoint` with a cache request (ControlType, Name, RuntimeId, BoundingRectangle, IsEnabled); menu highlight = the MenuItem under the pointer changed |
| `kAXSelectedTextAttribute` | `TextPattern::GetSelection` on the focused element while the left button is down; words and lines counted the same way |
| AXObserver slider / selection / scroll bar | polled on the UIA thread: `RangeValuePattern` of the element under the pointer while dragging (20 buckets), `SelectionItemPattern::IsSelected` of a TabItem/ListItem after a click, `ScrollPattern` percent of the scrollable ancestor after a wheel notch hitting 0 or 100 |
| `NSCursor.currentSystem` | `GetCursorInfo().hCursor` compared with the arrow and the known system cursors |
| window tiling via AX frame | foreground window rect (`DwmGetWindowAttribute EXTENDED_FRAME_BOUNDS`) 400 ms after a drag ends, matched against halves, quarters and thirds of the monitor work area |
| `NSWorkspace.didActivateApplication` | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` -> image name via `QueryFullProcessImageName` |
| `hidutil` per-device pointer speed | `SystemParametersInfo(SPI_SETMOUSESPEED / SPI_SETMOUSE / SPI_SETWHEELSCROLLLINES)` applied while the Surface Mouse is the last device that moved the pointer, restored (debounced 300 ms) when another pointing device moves; original values saved once |
| Force Click (pressure events) | **hold action**: left button held still (4 px or less) for `forceClickMs` -> forceClick pulse + the configured action: context menu (synthesised left-up at the origin, right down/up; the real left-up is swallowed), middle click, Action Ring, or none. Default: context menu, matching Windows' touch press-and-hold idiom |
| `ActionRunner` | Task View (Win+Tab) for Mission Control, Start search (Win+S) for Spotlight, Snipping Tool (Win+Shift+S, tagged) for screenshot, `LockWorkStation`, media/volume virtual keys, Win+D, Win+Ctrl+Left/Right for desktops, `ShellExecute` for apps, PowerShell for shell, brightness via WMI `WmiMonitorBrightnessMethods` (laptop panels; a no-op with a log line elsewhere) |
| menu bar extra | tray icon: left click opens settings, right click a native menu (Enable, Haptics, Settings, Setup guide, About, Quit) |
| Input Monitoring / Accessibility | none. The setup guide covers pairing, the haptics collection opening, and the one real Windows limitation: a non-elevated process does not receive or shape input aimed at elevated windows (UAC prompts, admin apps). Documented, not worked around |

### 3.6 UI

Direct2D 1.1 + DirectWrite on a per-monitor-DPI-v2 window with the Windows 11 shell: Mica through the frame and
the client (the frame is extended over the client, the target paints with per-pixel alpha, the solid base is the
fallback without Mica), the system title bar in the theme's tone. `Palette` holds the Fluent tokens, named after
the WinUI resources they stand in for; the accent is Windows' own from the Explorer accent ramp (light 2 on dark,
dark 1 on light) unless `settings.accentHex` names one; theme follows `settings.theme` (system via
`AppsUseLightTheme`, refreshed on `WM_SETTINGCHANGE` and the DWM colorization message). Controls are drawn, not
native, and are the Fluent ones: toggle, slider, selector bar, combo box with its flyout, number box, button, icon
button, check box, list row, navigation row; the text field is a themed native `EDIT` child (layered, so it stays
opaque over Mica) for the few free-text inputs. Type is Segoe UI Variable, Display from 20 px up and Text below,
nothing under 12 px. The window is a navigation pane (the live mouse at its head: buttons light from Raw Input, the
wheel turns, the action button glows, pulse rings on every haptic; then Haptics, Action button, Action Ring, Apps,
Pointer and scroll, Diagnostics; the master switch and About at the foot) beside a content layer with the page
title and the page as a stack of settings cards. The Action Ring is a layered, topmost, click-through
(`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`) window drawn with the same renderer, polled at 90 Hz
like the Mac, in the same accent. Plan 8 (`plan-8-fluent-shell.md`) has the details.

### 3.7 Services

- **Bridge**: Winsock listener on 127.0.0.1:`bridgePort` (47337), one thread, `Connection: close`, 8 KB
  body cap, 2 s read timeout, token from settings shown in Diagnostics. A raw socket needs no http.sys URL
  reservation.
- **Updater**: WinHTTP `GET api.github.com/repos/knightynite/ixpat/releases` once a day; Windows releases
  are tagged `win-vX.Y.Z` and marked pre-release so the Mac updater's `releases/latest` never sees them;
  the Windows updater picks the highest `win-v` tag with an asset matching `-windows-<arch>.zip`. A
  download is only auto-opened when it carries a valid Authenticode signature from a pinned publisher;
  until releases are signed the updater opens the release page instead (fails closed, like the Mac's
  Team ID check).
- **Single instance** via a named mutex; a second launch asks the first to show settings. A launch that finds
  the mutex held but no window (a relaunch right after `--quit`, while the previous instance is still leaving)
  waits for the mutex, up to 5 s, and then runs. A mutex that cannot be created at all stops the launch with a
  message: two unguarded instances would both shape the input.
- **Start at login** via the HKCU `Run` key.
- **Logs**: `%LOCALAPPDATA%\iXpat\debug.log` and `reports.log`, 512 KB rotation, never key or text
  content, exactly the Mac policy.
- **Crash dumps**: an unhandled exception, a stack overflow, abort(), an uncaught C++ exception or a CRT fault
  writes a minidump beside the logs (`crash-<time>-<pid>.dmp`, the last five kept) and one line in debug.log,
  then the process ends as it would have. A dump thread and the paths are prepared at startup, so the fatal
  path allocates nothing, takes no lock and does not use the logger. A dump is a copy of the process's memory
  at that moment: it can hold the settings, the chord state and recent input reports, which is why it stays on
  the PC and is never sent anywhere; the installed readme says so. `iXpat.exe --crash[=abort|throw|overflow|param]`
  dies on purpose to prove the handler on a given machine.
- **`--quit`** asks the running instance to close and waits until its window and then its instance mutex are
  gone (5 s each), so a script or an installer that relaunches right after finds the way clear; it returns 1 when
  an instance remains. A launch that finds the mutex held for more than 5 s with no window returns 1 too, with a
  line in debug.log. Command-line options are matched as whole tokens.

## 4. Error handling

- Hooks or Raw Input registration failing: the app runs haptics-only and the setup guide says so.
- Haptics collection missing or refusing to open: everything else runs; Diagnostics shows the error code.
- A hook callback overrunning: never by design (no I/O); the heartbeat re-installs if Windows drops it.
- Settings file unreadable: kept as `settings.broken.json`, defaults used, logged.
- Device sleeps mid-hold: gesture mode ends, pending hold action cleared, ring closed (Mac behaviour).
- Bridge: malformed requests get 400/401/404 JSON bodies; no body is ever logged.
- Updater: any network or JSON error is logged; the manual check reports it in a dialog.

## 5. Testing

- `core/` has doctest coverage for every unit listed in 3.4, including a Mac-written `settings.json`
  fixture round-trip and byte-exact JSON output ordering.
- `ixpat-probe` is the hardware instrument: with the mouse connected it logs HID collections, opens
  0x0E, reads 0x42, plays each waveform, logs Raw Input and hook events with QPC timestamps and reports
  how often the hook saw an event before its `WM_INPUT` (the attribution fallback rate).
- CI (GitHub Actions `windows-latest`) builds x64 and ARM64 with `/W4 /WX`, runs the unit tests on x64,
  and packages zips on `win-v*` tags.
- Each milestone's diff gets an independent read-only review, fed the diff
  inline, before it is considered done; findings are fixed or recorded with a reason.

## 6. Release and docs

- Assets per release: `iXpat-<ver>-windows-x64.zip`, `iXpat-<ver>-windows-arm64.zip`,
  `iXpat-Setup-<ver>-x64.exe` (Inno Setup, per-user install into `%LOCALAPPDATA%\Programs\iXpat`, optional
  start-at-login, no elevation). Unsigned until a signing certificate exists; the README says what
  SmartScreen will show and why.
- README: platform badges, Windows install/build sections, the Windows limitation, the corrected device
  facts. `docs/index.html`: a Windows section with its own download, FAQ entries, updated title and
  schema. `docs/windows-plan.md`: rewritten as the status of the port with the CFU finding and the
  measurement results, replacing the plan.
