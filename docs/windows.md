# iXpat for Windows

The Windows port lives in [`windows/`](../windows/) and ships as `win-vX.Y.Z` releases: a zip for
x64 and one for ARM64, and a per-user installer for x64, all signed. The spec is
[`windows/docs/design.md`](../windows/docs/design.md); what the mouse exposes on Windows, measured,
is [`windows/docs/measurements-2026-10.md`](../windows/docs/measurements-2026-10.md).

## What the port does

| Area | Windows | Notes |
|---|---|---|
| Haptics | yes | Win32 HID on the haptics collection (usage page 0x0E): feature 0x42 reads the waveform table, output 0x43 plays. Nothing else is ever written to the mouse. |
| Rules, presets, per-app profiles, master gates | yes | Same `settings.json` schema as the Mac, same event vocabulary; `"platform": "windows"` and a `"windows"` object hold the few Windows-only knobs. |
| Click, wheel, hold, free-spin, dynamic ratchet, kinetic scroll | yes | Raw Input tells the device, the low-level hooks shape (swallow, re-emit through `SendInput` from threads of their own). |
| Action button: press, double press, hold | yes | Win+Shift+S / Win + fake click / Win+Shift+F23 decoded by a platform-free gate replayed in tests; each answer has a feel of its own; the hold runs a context menu, middle click or the ring. |
| Action Ring | yes | Layered Direct2D window, click-through, Escape cancels. |
| Menu, control, drop, text, slider, selection and scroll-end ticks | yes | UI Automation on a worker thread; cursor-shape hover on main. |
| Pointer settings for this mouse only | yes | Applied while the Surface Mouse drives the pointer, restored when another device does. |
| Plugin bridge | yes | Same loopback HTTP API and token. |
| Settings window | yes | A Windows 11 app: Mica, a navigation pane with the live mouse, settings cards, the Fluent controls, Segoe UI Variable, the Windows accent; light/dark, per-monitor DPI; keyboard navigation. |
| Updates | yes | Asks GitHub daily for a newer `win-v` release. An installed x64 copy downloads the installer, verifies its Authenticode signature against the publisher's name and installs it; zip copies open the release page. |
| Start at login | yes | HKCU Run value. |
| Crash reports | yes | A crash writes a minidump beside the logs; the next launch offers to bundle it with the logs for an issue, and nothing leaves the PC unless the user attaches it. |
| Elevated windows | no | Windows does not deliver input aimed at UAC prompts and admin tools to a normal process; the mouse works there, nothing is shaped. |
| Firmware updates | no | Windows Update does this through the mouse's CFU interface (see below). |

## Changes by release

### win-v0.2.14

* Fixes action-button recovery when a keyboard hook misses the mouse's modifiers: a replacement hook can
  still intercept the letter and open the Action Ring. A shortcut already delivered to Windows is not run twice.
* Hook replacements overlap and drain queued callbacks before retiring the old hooks. Chord state survives
  replacement, and quiet periods refresh the hooks without injecting keys or resetting Windows' idle timer.
* Health checks recover even if the first probe was missed. Persistent failures back off instead of disabling
  recovery for the session. Chord keys also drain a larger, time-bounded backlog of device reports.
* Adds recovery replay tests and an opt-in Windows desktop stress test for hook handover and idle recovery.

Validation: 189 automated tests (1,800 assertions), x64 and ARM64 builds, and a Windows desktop stress test
with 2,000 input events during repeated hook replacements. Physical checks covered repeated mouse-button
presses, followed by a successful press after idle with the diagnostic monitor stopped.

### win-v0.2.13

* Stops retaining keyboard-event traces in diagnostic files, the Diagnostics pane and its clipboard report.
  Mouse gestures, haptics and hook-health diagnostics keep working.
* Filters keyboard-event traces from older logs when creating an issue-report bundle. Existing log files and
  previously exported bundles are not rewritten; create a new bundle with this version before sharing logs.
* Adds regression coverage for live logging, exported ZIP contents and truncated log tails.

### win-v0.2.12

* Windows releases are full releases that never claim the repository's "latest", which stays with the Mac app
  (its updater reads `releases/latest`). The development pre-releases before 0.2.12 are gone.
* A new icon: a graphite shell on a pale plate, drawn by `windows\tools\make_icon.py`. The white Mac shell
  vanished at 16 px on a light taskbar; this one reads at every size on both taskbar colours.
* The notification-area icon is loaded at the shell's small size instead of a 32 px frame scaled down.
* The release job's scripts take the tag from the environment rather than from their own text.

### win-v0.2.11

* The download links in the README and on the site point at the release that holds the files; the 0.2.10
  links still named the 0.2.9 release page and were dead.
* `windows\release-check.ps1` runs before a release is published: the tag, `version.h`, the installer script
  and those download links must agree on one version, or the release job stops.
* `SHA256SUMS.txt` has Unix line endings, so `sha256sum -c` accepts it as downloaded.
* The installer stores file times in UTC; an installed `iXpat.exe` carries the build's real time on any PC.

### win-v0.2.10

* The installer an update downloaded is removed on the next launch, once it has run, instead of staying beside the
  logs until the next update.
* A scheduled update offer no longer opens on top of another dialog. While the crash notice, an issue report, a
  message or a file picker is up, the check waits for the next six-hourly tick; a check that completes while one is
  up asks again at that tick instead.

### win-v0.2.9

* Signed releases: the executables and the installer carry an Authenticode signature from a publicly trusted
  certificate (Azure Artifact Signing). SmartScreen names the publisher instead of warning about an unknown one.
* Updates install themselves on an x64 copy the installer put in place: the installer is downloaded from GitHub's
  fixed release URL, its signature must be valid and its signer's name must match the pinned publisher, and only
  then does the app quit, let the installer run silently and come back on the new version. Anything else opens
  the release page as before.
* Report an issue, from About: a zip beside the logs with the log tails, the settings without the bridge token
  and, only when chosen, the newest crash report; Explorer shows the zip and the GitHub issue page opens with the
  version, the Windows build and the mouse filled in. After a crash the next launch asks once whether to report it.
* The double press no longer opens the Start menu beside Task View: Windows' typematic repeat of the mouse's Win
  key, arriving after the Task View chord had released Win, looked like a fresh press; the repeat is now eaten.
  A second press while the first chord is still in flight gets its own Start mask too.
* Pointer and scroll moved up to second in the navigation, with "Start iXpat when you sign in to Windows" at its
  head; the Diagnostics switch is "Diagnostic Mode" on About; About keeps one button, Report an issue.

### win-v0.2.8

* Each answer of the action button has a feel: two crisp taps for a press, a low tap and a firm one for a double
  press, a slow swell when a hold engages and a light tick when it lets go.
* A keyboard hook probe behind a breaker: a tagged key goes through the keyboard hook while the mouse is in use;
  two misses in a row put the hooks back, at most every two seconds, and more than five such re-installs in a
  minute switch the probe off for the session.
* The Diagnostics page is shown only when switched on in About; the navigation pane names the mouse by its own
  product string; the site offers the same download button and card for macOS and Windows.
* Timing lines: any injection, hook install, monitor refresh or loop iteration that takes longer than it should
  writes one `slow:` line, four a second at most.

### win-v0.2.7

* No voluntary hook re-installs: the periodic refresh and the mouse probe are off; the hooks go back only when a
  report arrives with no callback, when a device comes or goes, or when the app asks.

### win-v0.2.6

* Chopped scrolling fixed: the shaped wheel frames were injected from the thread that owns the hooks, and
  `SendInput` waited tens to hundreds of milliseconds per call for other programs' hooks, long enough for
  Windows to drop the hooks (300 ms limit) and let raw notches through. All injection now runs on threads of its
  own; the Start-menu mask is queued the moment the chord's Win make is recognised.
* The hum on the ratchet's re-engage is off by default; its row stays on the Haptics page.

### win-v0.2.5

* A crash writes a minidump beside the logs (`crash-<time>-<pid>.dmp`, the last five kept) and one line in
  debug.log; `iXpat.exe --crash` raises one on purpose so the handler can be proved on any machine.
* A launch that finds the previous instance still leaving waits for it, up to 5 s, and then runs.
* The executable's fixed version block derives from the same three numbers as the version string.
* `build.ps1 -Analyze` runs the MSVC static analyser over the sources and `build.ps1 -Sanitize -Test` runs the
  tests under AddressSanitizer; both are clean.

### win-v0.2.4

* The chord decisions moved out of the input thread into a platform-free gate, with tests that replay the
  firmware's sequences exactly as the logs recorded them.
* Keyboard navigation in the settings window: Tab walks every control with the Fluent focus ring; Space, Enter,
  the arrows, Home and End work the controls; flyouts take the keyboard and fit the window.

### win-v0.2.3

* Settings window fixes: Mica only when both DWM calls succeed; the native text box follows DPI and commits its
  whole text; a lost render target repaints at once; flyouts select by row and close on a page change; light
  tertiary text at 4.5:1.

### win-v0.2.2

* The hooks go back to the head of the chain a second after any device change; a re-install asked by the
  watchdog or the stall watch waits for a quiet keyboard.
* The watchdog counts physical reports only: input injected by another process reaches Raw Input without passing
  the low-level hooks.
* With the ring open, the double press's click commits the ring and still closes the chord.

### win-v0.2.1

* The action button, redone: the hooks decide alone and never wait; Raw Input names the device a millisecond
  later and never fires an action; a mask key keeps the Win release from opening Start.
* Smooth scrolling without the per-notch stall; the coast clock keeps its phase across notches.
* A Windows 11 settings window and a Diagnostics page with hook timing, re-installs and system stalls.

### win-v0.2.0

* First Windows release: haptics, rules, presets, per-app profiles, the action button, the Action Ring, kinetic
  scrolling, the UI Automation ticks, pointer settings for this mouse, the plugin bridge, the installer.

## Firmware updates: the CFU interface

The 2nd Edition (PID 0880) exposes a Component Firmware Update interface: collection 05 (usage page 0xFF0B,
usage 0x104). Windows binds Microsoft's inbox `HidCfu` driver to it through
`SurfaceMouse2ndEditionFwUpdate.inf` (driver-store folder `surfacemouse2ndeditionfwupdate.inf_amd64_…`), with
the CFU usages (offer 0x8A/0x8E, payload 0x66/0x61, versions 0x62, `UseHidSetOutputReport=1`) and three payloads:
MCU1, MCU2 and HAPTIC. Collection 04 (0xFF07/0x92) is the Mouse and Keyboard Center channel, with no driver bound
and an unknown protocol.

So firmware updates for the 2nd Edition arrive through Windows Update, in the background, over the standard CFU
protocol. iXpat never opens the CFU or vendor collections; it shows the installed firmware version and leaves
updates to Windows. For Mac-only users the safe path to a firmware update is to pair the mouse with any Windows
PC once.

## Open items

1. Hook recovery was revised in 0.2.14 for missed presses after idle and during use. A shortcut whose letter
   reaches Windows before recovery cannot be taken back; continued hardware testing covers sleep, reconnects
   and interaction with other programs that install keyboard hooks.
2. A UIAccess-signed build if elevated windows turn out to matter to enough people.
3. The vendor channel (0xFF07) capture from Mouse and Keyboard Center, which would give per-device pointer
   settings on both platforms.
