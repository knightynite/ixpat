# iXpat for Windows, Plan 2: App core (tray app, HID haptics, Raw Input, hooks, router)

**Goal:** `iXpat.exe`, a tray application that loads the shared `settings.json`, opens the mouse's haptics collection, plays the haptic rules for clicks, wheel notches, screen edges, drags, snaps and app switches, shapes the wheel (dynamic ratchet, free-spin, kinetic / reversed scrolling), decodes and hides the action button's chords, runs the mapped actions and hold-and-move gestures, keeps per-app profiles, serves the plugin bridge, and logs like the Mac. No settings window yet (Plan 4); settings are edited in the JSON file and reloaded live.

**Architecture:** Two new pure-core units carry the Mac AppDelegate's logic (`HapticEngine`: scaling, rate limiting, echoes; `Router`: device events -> rules, timers, actions) and are unit-tested with fake sinks and a fake clock. Win32 glue in `src/win/` is one responsibility per file: logging, settings store + file watch, HID device thread, Raw Input, hooks + input thread (attribution, kinetic timer, gesture mode, hold action), tagged SendInput + action runner, foreground tracking, snap detection, pointer settings, tray, bridge server. `src/app/main.cpp` wires them like `applicationDidFinishLaunching`.

**Tech Stack:** as Plan 1, plus Win32 user32/hid/cfgmgr32/ws2_32/shell32/dwmapi/winmm.

**Spec:** `windows/docs/design.md` (sections 3.2, 3.3, 3.4, 3.5, 3.7, 4), `windows/docs/measurements-2026-10.md` (section 2a: attribution rules).

**Execution method chosen by the user:** native, independent review of the plan's diff at the end.

## Global Constraints

- Everything in Plan 1's Global Constraints.
- Hook procedures and the input thread's callbacks are `noexcept`, allocate nothing on the hot path (fixed-size ring buffers and `std::array`s, preallocated), hold at most one uncontended `SRWLOCK`, and never call COM, HID, logging or the UI.
- Threads talk through `PostMessage` to the main thread's message window (`WM_APP + n` with packed payloads or indices into preallocated tables) and through lock-protected `ConfigSnapshot` structs pushed from main (the Mac's `Locked<T>` + `pushConfig`).
- Only usage page 0x0E output report 0x43 is ever written to the device. Col04/Col05 are never opened for write by the app.
- Our own `SendInput` carries `dwExtraInfo = 0x49585054` and is never attributed or shaped.
- `IXPAT_ANY_MOUSE=1` (environment) makes every pointing device count as the Surface Mouse for development on a PC where it is not connected; it is logged on startup and never the default.
- Nothing from a hook or Raw Input is logged except counts, kinds and device identity; never key values, text, or pointer positions.
- Commit after every task with a `windows:` prefix.

## Review Focus

1. The hook thread stalls (debugger, antivirus, a paging storm) past 300 ms and Windows silently removes the hook: the heartbeat watchdog must re-install within 5 s and log it (Task 7).
2. The mouse sleeps while the action button is held (hold chord down, then Bluetooth drops): gesture mode must end, the pointer must be released, the pending hold action cleared, and no key stays stuck down (Tasks 5 and 10).
3. A second `iXpat.exe` is started: it must bring the first one's settings folder/menu up and exit, never run two hook sets (Task 10).
4. `settings.json` is deleted or replaced by garbage while the app runs: the watcher must reload defaults (or keep the last good settings), write `settings.broken.json`, never crash, never lose the bridge token (Task 2).
5. A plugin floods the bridge with 1000 requests per second: the HID queue is bounded (drop oldest), the socket loop stays responsive, and the UI thread never blocks (Tasks 3 and 11).

---

### Task 1: Logging

**Files:** `windows/src/win/paths.h/.cpp` (app data dir), `windows/src/win/log.h/.cpp`.
**Produces:**
```cpp
namespace ixpat::win {
std::filesystem::path appDataDir();            // %LOCALAPPDATA%\iXpat, created
std::filesystem::path settingsPath();          // appDataDir()/settings.json
struct FileLog {                               // append-only, 512 KB rotation to <name>.1
    explicit FileLog(std::filesystem::path file);
    void write(std::string_view line);         // thread-safe (SRWLOCK), prefixes an ISO-8601 UTC timestamp
};
void debugLog(std::string_view s);             // debug.log: device attach/detach, hook state, gestures, errors
void reportLog(std::string_view s);            // reports.log: raw button/key/vendor report summaries, never motion
}
```
Steps: write; smoke by logging from `main` and reading the file; commit `windows: app data paths and rotating logs`.

### Task 2: Settings store

**Files:** `windows/src/win/settings_store.h/.cpp`; **Test:** `windows/tests/test_settings_store.cpp` (uses a temp directory; this one Win32 unit is tested because file atomicity matters).
**Produces:**
```cpp
namespace ixpat::win {
class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file);
    Settings load();                            // defaults when missing; broken file -> settings.broken.json + defaults; logs
    void save(const Settings&);                 // atomic: write <file>.tmp, MoveFileExW(REPLACE_EXISTING)
    // Debounced save on the caller's thread: markDirty() then flushIfDue(now) every 100 ms from a timer; flush() on quit.
    void markDirty(const Settings&, Seconds now); bool flushIfDue(Seconds now); void flush();
    // External edits: starts a thread with ReadDirectoryChangesW on the directory; posts `message` to `hwnd`
    // when settings.json changes (debounced 300 ms); ignores our own writes (compares a write token).
    void watch(HWND hwnd, UINT message); void stopWatching();
};
}
```
Tests: round-trip through a temp file; garbage file -> defaults + `.broken.json` exists; `save` never leaves a partial file (write, then check no `.tmp` remains); `flushIfDue` writes once 300 ms after the last `markDirty`.
Commit `windows: settings store with atomic writes, debounce and file watch`.

### Task 3: HID haptics device

**Files:** `windows/src/win/hid_device.h/.cpp`.
**Produces:**
```cpp
namespace ixpat::win {
struct DeviceInfo { bool connected; std::string firmware; std::wstring path; std::optional<int> battery; std::vector<Waveform> table; bool tableFromDevice; std::string lastError; };
class HidDevice {
public:
    // All callbacks are invoked on the HID thread; the owner posts to main.
    std::function<void(const DeviceInfo&)> onChange;   // connect / disconnect / table read / battery
    void start(); void stop();
    void trigger(int ordinal, int intensity);           // enqueue (bounded 8, drop oldest); any thread
    DeviceInfo snapshot() const;                        // lock-protected copy
    void refreshBattery();
};
}
```
Implementation: thread with `CM_Register_Notification(GUID_DEVINTERFACE_HID)` + a 5 s re-enumeration timer; opens the 0x0E collection of VID 045E PID 0880 RW; reads feature 0x42 (`HidD_GetFeature`, `FeatureReportByteLength`), parses with `parseHapticTable`; writes output 0x43 padded to `OutputReportByteLength` via `HidD_SetOutputReport` (falls back to `WriteFile` once if SetOutputReport fails with `ERROR_INVALID_FUNCTION`); firmware from `HIDD_ATTRIBUTES.VersionNumber`; battery from the parent BTHLE device's property `{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2` every 15 min. `IXPAT_ANY_MOUSE` does not affect this unit (haptics need the real device).
Verify: with the probe-measured facts only (device absent): `start` logs "no Surface Mouse"; `trigger` is a no-op; no crash on `stop`. Commit `windows: HID haptics device thread`.

### Task 4: HapticEngine (core, tested)

**Files:** `windows/src/core/haptic_engine.h/.cpp`; **Test:** `windows/tests/test_haptic_engine.cpp`.
**Produces:**
```cpp
namespace ixpat {
struct EngineParams { bool enabled; bool effHapticsEnabled; int masterIntensity; };   // from Settings
class HapticEngine {
public:
    struct Play { int ordinal; int intensity; };
    struct Deferred { int delayMs; Play play; std::string label; };
    std::function<void(Play, std::string_view label)> trigger;     // -> HidDevice::trigger
    std::function<void(Deferred)> defer;                           // owner schedules the echo
    void setTable(std::span<const Waveform>);                      // durations for the rate limit
    void setConnected(bool);
    bool play(int ordinal, int intensity, std::string_view label, Seconds now);   // false when rate-limited / disconnected
    void play(HapticEvent e, const HapticRule& rule, const EngineParams& p, Seconds now, std::optional<int> ordinal = {}, int boost = 0);
    void preview(const HapticRule& r, std::string_view label, Seconds now);
    int pulseCount() const; std::string lastPlayed() const; std::string lastEvent() const;
};
}
```
Tests (from `HapticEngine.swift`): disconnected -> nothing; two plays within the waveform's duration -> the second is dropped (min 35 ms); master 4 and rule 2 -> 2; master 1 and rule 4 -> `max(1, round(4*1/4)) = 1`; master 0 -> nothing; boost +1 on rule 4 clamps to 4; disabled rule -> nothing; echo -> `defer` called with `max(20, delayMs)` and the scaled intensity; unknown ordinal -> 50 ms default duration.
Commit `windows: core haptic engine (scaling, rate limit, echo)`.

### Task 5: Router (core, tested)

**Files:** `windows/src/core/router.h/.cpp`; **Test:** `windows/tests/test_router.cpp`.
**Produces:** the Mac AppDelegate's event logic, free of Win32:
```cpp
namespace ixpat {
struct RouterSinks {
    std::function<void(HapticEvent, std::optional<int> ordinal, int boost)> play;   // -> HapticEngine::play(event, rule(e), params, now, ...)
    std::function<void(const ButtonAction&, bool hold)> runAction;                 // -> win::ActionRunner / ring / gestures
    std::function<void(int id, int delayMs)> armTimer;                             // owner sets a one-shot timer, calls Router::timer(id)
    std::function<void(int id)> cancelTimer;
    std::function<void()> gesturesBegin, gesturesEnd;                              // pointer freeze on/off
    std::function<void(bool holdMode)> ringOpen; std::function<void()> ringToggle; std::function<bool()> ringIsOpen;
};
class Router {
public:
    Router(Settings& s, RouterSinks sinks);
    void setTable(std::span<const Waveform>);          // ordinals for Collide/Step/Hover/Error
    // Device side (Raw Input, attributed to the Surface Mouse):
    void buttons(std::uint8_t oldMask, std::uint8_t newMask, Seconds now);
    void wheel(int notches, Seconds now);
    void chord(ChordOutput, Seconds now);              // from ChordDecoder
    void holdEnded(Seconds now);                       // ChordOutput::holdEnd routed here
    void gestureFired(const ButtonAction&, int firedCount, Seconds now);   // from GestureAccumulator moves (volume limit -> gestureLimit)
    // System side:
    void systemEvent(HapticEvent e, Seconds now);      // screenEdge, dragEnd, windowSnap, appSwitch, forceClick, ticks...
    void foregroundChanged(std::string imageName, Seconds now);
    void deviceConnected(bool);
    void timer(int id, Seconds now);                   // hold timers (ids 1 left, 2 right), free-spin idle (3)
    bool freeSpinActive() const;                       // for the kinetic multiplier
    WheelFeelParams wheelParams() const;               // derived from settings + table
};
}
```
Tests mirror `applicationDidFinishLaunching` wiring: left press plays `leftDown` and arms timer 1 with `clickHoldMs`; release cancels; timer 1 while still down plays `leftHold`; a wheel notch goes through `WheelFeelEngine` (free-spin on -> `freeSpinOn`; idle timer 3 -> `freeSpinOff`); `chord(press)` plays `actionButton` and runs `actions[press]`; `chord(holdBegin)` with `gesturesOnHold` and a non-ring action -> `gesturesBegin` and the action is deferred; `holdEnded` without gestures runs the deferred action, with gestures does not; `holdBegin` with the ring action -> `ringOpen(true)`; `foregroundChanged` sets `frontBundleID`, plays `appSwitch`; `deviceConnected(false)` ends gestures and clears the pending hold; `gestureFired(volumeUp)` plays `gestureStep`, `previousTrack` plays `gestureSwipe` with the Error ordinal.
Commit `windows: core router (the Mac AppDelegate's event logic)`.

### Task 6: Raw Input

**Files:** `windows/src/win/raw_input.h/.cpp`.
**Produces:**
```cpp
namespace ixpat::win {
enum class RawKind : std::uint8_t { move, button, wheel, hwheel, key, consumer, other };
struct RawEvent { RawKind kind; bool surfaceMouse; bool injected; std::int64_t qpc; std::int32_t a, b; std::uint16_t scan; std::uint16_t buttonFlags; };
class RawInput {
public:
    bool registerOn(HWND messageWindow);           // mouse, keyboard, consumer; RIDEV_INPUTSINK
    std::optional<RawEvent> decode(HRAWINPUT);     // GetRawInputData; resolves hDevice -> isSurfaceMouse (cached)
    bool anyMouseMode;                             // IXPAT_ANY_MOUSE
};
}
```
Verify with the probe's log approach reused (a `--raw-only` dev switch in the app is unnecessary; `ixpat-probe log` already covers it). Commit `windows: raw input decoding and device attribution`.

### Task 7: Hooks and the input thread

**Files:** `windows/src/win/input_thread.h/.cpp`, `windows/src/win/hooks.h/.cpp`, `windows/src/win/send_input.h/.cpp`.
**Produces:**
```cpp
namespace ixpat::win {
struct InputConfig {                       // pushed from main under a lock (the Mac's SmoothConfig + ForceConfig + gesture flags)
    KineticConfig kinetic; bool reverseOnly; bool shapeWheel;      // shapeWheel = device connected or recently detached, and enabled
    bool swallowChords; bool gestureActive; bool ringOpen;
    bool holdActionEnabled; int holdActionMs; HoldAction holdAction;
    bool anyMouse;
};
class InputThread {
public:
    // Posted to `main` as WM_APP_INPUT with a packed InputEvent (ring-buffer index): raw buttons/wheel/keys for the
    // Router and the chord decoder, system events (screenEdge, dragEnd, clickAt, pointerMoved), hook heartbeat.
    void start(HWND main); void stop();
    void setConfig(const InputConfig&);
    void setChordState(bool chordActive, bool swallowLeftClick);   // from the decoder on main, lock-protected
    void gestureMove(...)                                          // GestureAccumulator lives here; results posted to main
    std::uint32_t heartbeat() const;                               // increments on every hook callback; watchdog on main
};
// send_input.h: everything we synthesise, tagged 0x49585054
void sendWheelPixels(int dy, int dx, double pxPerNotch);  // pixels -> WHEEL_DELTA units with carry
void sendKeyChord(std::initializer_list<WORD> vks);       // down in order, up in reverse
void sendMouseButton(bool right, bool down, POINT at);
void sendMediaKey(WORD vk);
bool isOurs(ULONG_PTR extraInfo);
}
```
Behaviour (hook thread, from `EventMonitor.handle`): clicks/keys stop the coast; left down while the ring is open -> consumed (ring commit) and its release hidden for 1.5 s; right down -> ring cancel likewise; chord keys swallowed while `chordActive` and the key is Win/Shift/S/F23 and (attributed to the mouse or within the window); the fake left click swallowed while `swallowLeftClick`; wheel: injected -> pass; `shapeWheel` and attributed (or no other device scrolled in 2 s) -> feed `Kinetic` (or negate for reverse-only) and swallow; moves while `gestureActive` -> consumed, `SetCursorPos` back if needed; drag tracking (20 px), screen edge (cached monitor rects, `WM_DISPLAYCHANGE`), hold action: left down still for `holdActionMs` -> post `forceClick`, perform the hold action, swallow the real left-up. 120 Hz coast timer via `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` in the thread's `MsgWaitForMultipleObjectsEx` loop. Heartbeat watchdog: main checks every 5 s; if raw input arrived but no hook callbacks, re-install and log.
Verify by running the app with `IXPAT_ANY_MOUSE=1` using another mouse: kinetic scrolling in Notepad/Edge, reverse scroll, logs. Commit `windows: input thread with hooks, attribution, kinetic scroll, gesture mode, hold action`.

### Task 8: Action runner, foreground, snap, pointer settings

**Files:** `windows/src/win/actions.h/.cpp`, `windows/src/win/foreground.h/.cpp`, `windows/src/win/snap.h/.cpp`, `windows/src/win/pointer_settings.h/.cpp`.
**Produces:**
```cpp
namespace ixpat::win {
void runAction(const ButtonAction&);     // Task View Win+Tab, Search Win+S, Snipping Tool Win+Shift+S, LockWorkStation,
                                         // media/volume VKs, Win+D, Win+Ctrl+Left/Right, ShellExecute, PowerShell -NoProfile -Command
std::optional<float> systemVolume();     // IAudioEndpointVolume, for gestureLimit
class ForegroundWatch { void start(std::function<void(std::string imageName, DWORD pid)>); void stop(); };  // SetWinEventHook EVENT_SYSTEM_FOREGROUND
std::optional<RECT> foregroundFrame();   // DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)
bool looksSnapped(const RECT& before, const RECT& now);   // halves, quarters, thirds of the monitor work area, 12/24 px slack
class PointerSettings { void apply(double speed, bool noAccel, double scrollSpeed); void restore(); };  // SPI_*, saves originals once
}
```
Verify: each action manually once; `looksSnapped` has unit tests in `tests/test_snap.cpp` (pure RECT math, moved to `core/snap.h` so it is testable without Win32). Commit `windows: action runner, foreground watch, snap detection, pointer settings`.

### Task 9: Bridge server

**Files:** `windows/src/win/bridge_server.h/.cpp`.
Winsock listener on 127.0.0.1:port, one thread, blocking accept, per-connection read with 2 s `SO_RCVTIMEO`, feeds `parseHttpRequest`, `BridgeHandler::handle`, `serializeResponse`, closes. `onPlay` posts to main (ring buffer of names). Verify with `curl` against the running app: `/ping`, `/waveforms`, `/haptic` with and without the token. Commit `windows: bridge server`.

### Task 10: Tray app and wiring

**Files:** `windows/src/win/tray.h/.cpp`, `windows/src/win/single_instance.h/.cpp`, `windows/src/app/main.cpp`, `windows/src/app/version.h`, `windows/res/ixpat.rc`, `windows/res/ixpat.ico` (rendered from `Resources/AppIcon-1024.png` by `tools/make_ico.py`), `windows/src/app/app.manifest` (per-monitor v2 DPI, Windows 10/11 compat GUIDs, no elevation).
`main.cpp`: single instance (named mutex `Local\iXpat.{token}`; second instance posts `WM_APP_SHOW` to the first and exits), message-only window, `SettingsStore`, `HidDevice`, `HapticEngine`, `Router`, `ChordDecoder`, `InputThread`, `ForegroundWatch`, `BridgeServer`, `PointerSettings`, tray (icon states: connected / disconnected / disabled; menu: Enabled, Haptics, Open settings file, Open logs folder, Reload settings, About (version, firmware, battery), Quit), timers (hold 1/2, free-spin idle 3, settings flush 4, watchdog 5, battery 6), `pushConfig()` after every settings change, graceful quit (flush settings, restore pointer, end gestures, unhook). CMake: `add_executable(ixpat WIN32 ...)`, `OUTPUT_NAME iXpat`, link `user32 shell32 hid cfgmgr32 ws2_32 dwmapi ole32 winmm`. CI uploads `iXpat.exe`.
Verify: launch, tray shows, log shows hooks + raw input registered, settings.json created, edit it externally -> reload logged, second launch exits, Quit cleans up (Task Manager shows no process). Commit `windows: tray application wiring`.

### Task 11: Review and close-out

independent review of the Plan 2 diff (inline), fix confirmed findings with tests where testable, record declined findings in `windows/docs/reviews/plan-2-review.md`. Commit `windows: plan 2 review fixes`.
