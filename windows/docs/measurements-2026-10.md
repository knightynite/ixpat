# Measurements, October 2026

What `ixpat-probe` and the system itself said about the Surface Mouse (2nd Edition) on Windows 11 24H2
(build 26300, this development PC). Facts first, interpretation after. Items marked **pending** need the
mouse connected to this PC (it is usually paired to the Mac).

## 1. Device enumeration (PnP, 2026-10-22, mouse paired but asleep)

Six HID collections under `HID\{00001812-...}_DEV_VID&02045E_PID&0880_REV&0529_<addr>&COLnn`
(Bluetooth LE HID over GATT, firmware 5.29):

| Col | Usage | Driver | Note |
|---|---|---|---|
| 01 | 0x0001/0x0002 mouse | mouhid | buttons, X/Y, wheel; Raw Input only |
| 02 | 0x0001/0x0006 keyboard | kbdhid | the action button's chords; Raw Input only |
| 03 | 0x000C/0x0001 consumer | hidserv | media keys |
| 04 | 0xFF07/0x0092 vendor | none | Mouse and Keyboard Center channel; protocol unknown; never written |
| 05 | 0xFF0B/0x0104 vendor | HidCfu (UMDF) via `SurfaceMouse2ndEditionFwUpdate.inf` 1.0.5.0 | Component Firmware Update: offer 0x8A/0x8E, payload 0x66/0x61, versions 0x62; `UseHidSetOutputReport=1`, `Alignment=1`; payloads MCU1, MCU2, HAPTIC in the driver store |
| 06 | 0x000E/0x0001 Simple Haptic Controller | none (HID class only) | output 0x43, feature 0x42 |

Correction to the macOS plan: a 2nd Edition firmware package does exist (September 2026, delivered by
Windows Update into the driver store on 2026-10-17 here), and the "unknown" 0xFF0B collection is
Microsoft's documented CFU protocol. Col04 (0xFF07) remains the only unknown channel.

## 2. Raw Input vs low-level hook ordering

### 2a. Injected input (SendInput), measured 2026-10-22 with `ixpat-probe log 3 --inject`

For every injected mouse move the **hook fired first** and the matching `WM_INPUT` arrived
0.1 to 0.3 ms later; the injected wheel notch and the second key event were found by the in-hook
`PeekMessage(WM_INPUT)` drain, the first key event was not. Matching by kind alone therefore paired
hook event N with raw event N-1 for the move stream (an off-by-one that looked like a match).

Consequences for the design, adopted:

- Attribution matches on **content**, never on kind alone: wheel by delta, buttons by flag/message,
  keys by scan code (`RAWKEYBOARD.MakeCode` == `KBDLLHOOKSTRUCT.scanCode`; Raw Input reports
  `VK_SHIFT` where the hook reports `VK_LSHIFT`, so virtual keys alone do not match).
- The hook must be able to decide before the raw event arrives. For wheel notches the rule is the
  Mac's: shape every non-injected wheel notch while the Surface Mouse is connected and no other
  pointing device has produced a wheel report in the last 2 s; otherwise fall back to the content
  match against the raw FIFO (either order) within 50 ms.
- Our own `SendInput` events are recognised by `dwExtraInfo == 0x49585054` in the hook and by
  `hDevice == NULL` in Raw Input, and are never attributed to any device.

### 2b. Real device: **pending**

Run with the mouse connected: `ixpat-probe log 60 --out probe-real.txt` while moving, scrolling,
clicking and pressing the action button, with a second mouse or trackpad in use at the same time.
Record: hook-before-raw rate per kind, drain hit rate, and whether real HID input orders differently
from injected input.

## 3. Haptics collection: **pending**

- `ixpat-probe list`: does `CreateFile(GENERIC_READ|GENERIC_WRITE)` on Col06 succeed while Windows'
  own haptics support is active? Expected yes (no driver is bound to the collection).
- `ixpat-probe table`: feature 0x42 bytes and the parsed table; compare with the macOS table
  (ordinals 3..10: Hover 9 ms, Collide 26, Align 9, Step 11, Grow 200, Success 70, Error 147, Click 11).
- `ixpat-probe play` and `ixpat-probe play --writefile`: success per API, round-trip microseconds, and
  whether every waveform is felt.
- `ixpat-probe versions`: the CFU versions feature report (read-only) and the component versions it
  carries.
