# Plan 2 review (independent review, 2026-10-23)

Independent read-only review of the Plan 2 diff (Win32 application core), fed inline. 2 Critical, 20 Important,
6 Minor. Triage by the author; "fixed" means the change is in the following commit.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| C1 | Critical | Bridge thread iterated `device_.table` while main replaced it | Fixed: main publishes a JSON snapshot under an SRW lock; the bridge thread copies the string. |
| C2 | Critical | Settings watcher closed handles with the directory read in flight after `CancelIoEx` | Fixed: `GetOverlappedResult(..., TRUE)` waits for the cancelled request before the buffers go away. |
| I1 | Important | Hook copied a config holding `ButtonAction` strings under the lock | Fixed: `GestureConfig` now carries `ActionKind`s only; `InputConfig` is plain data; `GestureAccumulator::move` returns a fixed array of directions (no vector). |
| I2 | Important | In-hook drain unbounded, resolved device names, could re-enter | Fixed: drain bounded (32), only from the outermost callback (`hookDepth_`), `PM_NOYIELD`; device identity is resolved at start (`GetRawInputDeviceList`) and on `WM_INPUT_DEVICE_CHANGE` (`RIDEV_DEVNOTIFY`), removals forget the handle; an unknown handle inside a drain is reported as unknown, never looked up. |
| I3 | Important | `raw_.anyMouse` plain-bool race | Fixed: requested through an atomic, applied on the input thread. |
| I4 | Important | Startup handshake closed `ready` while the worker might still signal; HWND unsynchronised | Fixed: `ready_` lives with the object, timeout stops and joins; `wnd_` is atomic. |
| I5 | Important | Raw registration / partial hook failures ignored | Fixed: all-or-nothing hook install, status struct (`rawRegistered`, per-hook flags) logged at start and shown in About; the app runs haptics-only. |
| I6 | Important | A mouse packet became one event (wheel + buttons lost) | Fixed: `RawInput::decode` expands a packet into button, wheel and motion components. |
| I7 | Important | Late raw entries could pair with a later identical hook event; key identity incomplete | Fixed: a hook that decided before its raw entry records an expectation; the matching raw entry is retired on arrival. Keys match on scan code + make/break + E0. |
| I8 | Important | Only vertical foreign wheel input inhibited the fallback | Fixed: both axes. |
| I9 | Important | Injected keyboard events could consume raw entries / be swallowed | Fixed: injected keys return before matching. |
| I10 | Important | HID removal ignored while the device was open | Fixed: on an interface change the worker checks that its path is still present and closes otherwise. |
| I11 | Important | A trickling bridge client could block `stop()` | Fixed: the active client socket is tracked and closed by `stop()`; 5 s absolute request deadline; `stopping_` checked in loops. |
| I12 | Important | Disabling did not end gestures / holds / the coast | Fixed: the input thread applies a disable transition (stop coast, disarm hold, end gesture); main calls `Router::reset()`; `fireHoldAction` re-checks the flags. |
| I13 | Important | Gesture mode ended asynchronously; late moves enqueued actions | Fixed: the input thread ends the accumulator on the chord's `holdEnd`; main drops gesture actions when the router is no longer in gesture mode. |
| I14 | Important | Foreground change did not push the config | Fixed. |
| I15 | Important | Hold action needed a matched raw down; plain mice excluded | Fixed in part: unmatched downs use the same live-and-quiet fallback as the wheel. **Ruling:** the hold action stays Surface-only (the spec's "this mouse"), not any plain mouse. |
| I16 | Important | Hold injection ignored `SendInput` results | Fixed: the real release is swallowed only after the synthetic release succeeded. |
| I17 | Important | Pointer settings applied globally with no active-device tracking | Fixed: Raw Input motion reports the pointer's owner; main applies / restores after a 300 ms debounce. |
| I18 | Important | 1800 s detach allowance never expired on its own | Fixed: a timer pushes the config when it lapses. |
| I19 | Important | Ring overflow dropped transitions; failed `PostMessage` wedged `posted_` | Fixed: ring 4096, overflow flag surfaces as an event that resets gesture / hold state; `posted_` is reset on a failed post. |
| I20 | Important | One heartbeat could not tell which hook died | Fixed: mouse and keyboard hook calls are compared against their own raw streams. |
| M1 | Minor | HID buffers fabricated lengths | Fixed: exact descriptor lengths; too-short descriptors are rejected / fall back to the measured table. |
| M2 | Minor | Queued `BridgePlay` payloads leaked on destroy | Fixed: `quit()` stops the producers first and drains the queue before `DestroyWindow`. |
| M3 | Minor | Unbalanced `WSAStartup` | Fixed: once per server, `WSACleanup` in `stop()`. |
| M4 | Minor | Single `send` | Fixed: loop until sent. |
| M5 | Minor | Frozen-pointer anchor can drift under injected moves | **Deferred.** Injected moves are rare while a hold gesture is in progress. |
| M6 | Minor | `none` returned early before gesture-mode entry | Fixed with a test (the Mac starts gesture mode on a hold with no action). |

Missing tests the reviewer listed (input ordering replays, lifecycle stress, state transitions) are integration
tests that need the real device or a harness around the hook thread; the probe covers ordering on hardware.
