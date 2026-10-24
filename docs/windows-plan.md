# iXpat for Windows: status and plan

The Windows port lives in [`windows/`](../windows/) and ships as `win-vX.Y.Z` releases
(zip for x64 and ARM64, a per-user installer for x64). This page is the status of that
port and the record of what the first plan (October 2026) got wrong once the mouse was
measured on a Windows PC. The spec is [`windows/docs/design.md`](../windows/docs/design.md);
the measurements are [`windows/docs/measurements-2026-10.md`](../windows/docs/measurements-2026-10.md).

## 1. What shipped (win-v0.2.0)

| Area | Windows | Notes |
|---|---|---|
| Haptics | yes | Win32 HID on the haptics collection (usage page 0x0E): feature 0x42 reads the waveform table, output 0x43 plays. Nothing else is ever written to the mouse. |
| Rules, presets, per-app profiles, master gates | yes | Same `settings.json` schema as the Mac, same event vocabulary; `"platform": "windows"` and a `"windows"` object hold the few Windows-only knobs. |
| Click, wheel, hold, free-spin, dynamic ratchet, kinetic scroll | yes | Raw Input tells the device, the low-level hooks shape (swallow, re-emit through `SendInput`). |
| Action button chords, gestures, hold action | yes | Win+Shift+S / Win + fake click / Win+Shift+F23 decoded on the input thread; the hold runs a context menu, middle click or the ring. |
| Action Ring | yes | Layered Direct2D window, click-through, Escape cancels. |
| Menu, control, drop, text, slider, selection and scroll-end ticks | yes | UI Automation on a worker thread; cursor-shape hover on main. |
| Pointer settings for this mouse only | yes | Applied while the Surface Mouse drives the pointer, restored when another device does. |
| Plugin bridge | yes | Same loopback HTTP API and token. |
| Settings window | yes | A Windows 11 app: Mica, a navigation pane with the live mouse, settings cards, the Fluent controls, Segoe UI Variable, the Windows accent; light/dark, per-monitor DPI. |
| Updates | check only | Asks GitHub daily for a newer `win-v` release and opens the release page. The builds are unsigned, so nothing installs itself. |
| Start at login | yes | HKCU Run value. |
| Elevated windows | no | Windows does not deliver input aimed at UAC prompts and admin tools to a normal process; the mouse works there, nothing is shaped. A UIAccess-signed binary would be needed. |
| Firmware updates | no (see 3) | Windows Update already does this. |

### win-v0.2.1

* **The action button, redone.** The hooks decide alone and never wait: Windows runs the keyboard hook before it
  generates a key's Raw Input report (and generates none for a key the hook ate), so a hook that waited for the
  report could only stall the system's input thread. Raw Input names the device a millisecond later and never
  fires an action; a report the hook never saw means Windows dropped the hook and already acted, so nothing fires
  twice, and the hooks are put back within 50 ms (and every 5 s while idle regardless). A mask key keeps the Win
  release from opening Start. The decoder fires only on keys it was fed; its time-based guesses are gone.
* **Smooth scrolling.** No more 2 ms stall per notch; the coast clock keeps its phase across notches.
* **A Windows 11 window** (above), and a Diagnostics page that shows hook timing, re-installs and system stalls.
* **Still open.** Windows drops the low-level hooks of this process now and then, with our callbacks at tens of
  microseconds; the stall watch is there to show whether the machine stands still when it happens.

### win-v0.2.2

* **The hook chain.** Both hook losses seen with real input were the first press after the mouse reconnected: a
  program that re-installs its hooks on a device change goes ahead of ours, and one that does not chain starves
  everything behind it. The hooks now go back to the head of the chain a second after any device change, and a
  re-install asked by the watchdog or the stall watch waits for a quiet keyboard instead of running at once.
* **The watchdog counts physical reports only.** Input injected by another process reaches Raw Input without
  passing the low-level hooks (measured with a standalone probe), so comparing all reports with hook calls raised
  false alarms.
* **Chords.** With the ring open, the double press's click commits the ring and still closes the chord; a swallowed
  letter whose release never came is the user's again after two seconds either way.
* **The text box** in the settings window takes the theme's colours.

### win-v0.2.3

* **The settings window, reviewed.** Mica only when both DWM calls succeed; the native text box follows DPI and
  the field's monospaced style, commits its whole text, keeps its commit when the page changes, and is committed
  once its field scrolls out of view; a lost render target repaints at once; a press ends on a lost capture
  without activating; flyouts select by row, left button only, and close on a page change; ring rows fit the card
  at every width; light tertiary text at 4.5:1; a custom accent's text follows its brightness. Known gaps:
  keyboard navigation between the drawn controls, flyouts taller than the window.

### win-v0.2.4

* **The action button, replayed.** The chord decisions moved out of the input thread into a platform-free gate,
  and twelve tests replay the firmware's sequences exactly as the logs recorded them (the press, the press with the
  hook gone, the user's own keys, the double press, the hold with repeats, a letter the user already holds, a stray
  letter during a hold, a hold whose breaks never come, a re-install, another keyboard's report, the ring open,
  hiding switched off).
* **Keyboard.** Tab walks every control with the Fluent focus ring; Space, Enter, the arrows, Home and End work
  the controls; flyouts take the keyboard and fit the window.

### win-v0.2.5

* **A crash leaves evidence.** An unhandled exception writes a minidump beside the logs
  (`%LOCALAPPDATA%\iXpat\crash-<time>.dmp`, the last five kept) and one line in debug.log, then the process ends
  as it would have. `iXpat.exe --crash` raises one on purpose, so the handler can be proved on any machine.
* **Relaunch right after a quit.** A launch that finds the previous instance still leaving (its window gone, the
  instance mutex still held) waits for it, up to 5 s, and then runs; before, it exited silently, which an
  installer or a script that quits and relaunches could trip over.

## 2. What the first plan got wrong

* **Language and UI.** The plan weighed Rust + WinUI 3 or egui. The port is C++20 with
  Win32 and Direct2D in one static binary: no runtime, no WebView, one process, and the
  hook, Raw Input, HID and UIA code is plain Win32 either way.
* **Device attribution.** "Correlate by arrival order and delta" became: the hooks match
  raw entries on content (wheel delta, button flag, scan code) in either order, with a
  fallback when the Surface Mouse is live and no other device acted within 2 s, and
  injected input is never attributed.
* **Collections.** On Windows the 2nd Edition exposes six HID collections, not the five
  read on macOS (see 3).

## 3. Firmware updates: the CFU finding

The first plan said the 2nd Edition (PID 0880) had no published firmware package and no
CFU collection. Both were wrong once the mouse was paired to a Windows 11 PC
(firmware 5.29, 2026-10-22):

* Collection 05 (usage page 0xFF0B, usage 0x104) is a **Component Firmware Update**
  interface. Windows binds Microsoft's inbox `HidCfu` driver to it through
  `SurfaceMouse2ndEditionFwUpdate.inf` 1.0.5.0 (driver-store folder
  `surfacemouse2ndeditionfwupdate.inf_amd64_…`), with the CFU usages (offer 0x8A/0x8E,
  payload 0x66/0x61, versions 0x62, `UseHidSetOutputReport=1`) and three payloads:
  MCU1, MCU2 and HAPTIC.
* Collection 04 (0xFF07/0x92) remains the Mouse and Keyboard Center channel with no
  driver bound and an unknown protocol.

So firmware updates for the 2nd Edition arrive through Windows Update, in the
background, over the standard CFU protocol, and iXpat has no reason to implement them:
it never opens the CFU or vendor collections and shows the installed firmware version;
updates arrive through Windows Update on their own. The macOS plan for an in-app updater is withdrawn for the
same reason: the safe path for Mac-only users is to pair the mouse with any Windows PC
once.

## 4. What is next

1. Measurements with the mouse on the Windows PC for the parts only hardware can answer
   (sections 2b and 3 of the measurements doc): the exact report lengths the Windows
   stack accepts, battery via the Bluetooth LE property, and the waveform durations.
2. A UIAccess-signed build if elevated windows turn out to matter to enough people.
3. The vendor channel (0xFF07) capture from Mouse and Keyboard Center, which would give
   per-device pointer settings on both platforms.
