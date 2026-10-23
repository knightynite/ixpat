# Codex review of Plans 3–5 (ring, settings UI, UI Automation ticks) and the triage

Reviewer: Codex CLI (`gpt-6-astra`), read-only, 2026-10-23. Prompt: the spec, plans 3–5, the Mac's RingWindow /
AXWatcher / MenuWatcher / HoverWatcher sources, and the full `src/ui`, `src/win/uia_watch.*`, `src/app/main.cpp`.
Every finding was checked against the code before acting; "confirmed" below means the defect was reproduced by
reading the exact lines (and, for the toolkit, by a headless test that failed before the fix).

## Findings and what happened

| # | Severity | Finding (Codex) | Verdict | Action |
|---|---|---|---|---|
| 1 | Critical | Popups keep `int*` into caller-local indices; the selection lands in dead stack memory after `finish()` | confirmed | Popups carry the current value; a row click is recorded by id and consumed by that dropdown on a rerun input pass (`UiState::pendingPopupId`, `rerun`). `tests/test_im.cpp` |
| 2 | Critical | `textField` calls `beginEdit` before setting `editTarget`: first click opens nothing, the next edits the previous field | confirmed | Target and id are set before the owner creates the EDIT |
| 3 | Critical | A settings reload invalidates EDIT targets and references held across a file dialog | confirmed (plausible path) | `App::reloadSettings` cancels editing first; a reload arriving while `GetOpenFileName` pumps messages is deferred until the dialog returns (`modalDepth_`, `reloadPending_`) |
| 4 | Important | The mouse-down that opens a dropdown is handled again by `finish()` and closes it | confirmed (no dropdown ever opened) | Popup hit-testing moved to the start of the next pass against the previous pass's popups; the opening click only opens. `tests/test_im.cpp` |
| 5 | Important | Enter / Escape never reach the parent while the native EDIT has focus | confirmed | The EDIT is subclassed (`SetWindowSubclass`); Enter/Escape post commit/cancel to the owner, no beep |
| 6 | Important | `DestroyWindow(edit_)` re-enters through `EN_KILLFOCUS`; Escape could commit | confirmed | State is detached before `DestroyWindow`; `EN_KILLFOCUS` is ignored once detached; focus is restored only when the window is active |
| 7 | Important | Pressing a modifier first cancels shortcut recording; Alt combos arrive as `WM_SYSKEYDOWN` | confirmed | Modifiers keep recording; `WM_SYSKEYDOWN` handled |
| 8 | Important | A recorded shortcut is applied during a paint pass, so the change is never saved/pushed | confirmed | Recording ends with an explicit empty input pass |
| 9 | Important | Apps pane editor width is `230 − 230 − 16 = −16` | confirmed | Full width captured before the list sub-area |
| 10 | Important | `hwnd_` unset during creation messages; cleared at `WM_DESTROY` | confirmed | Bound in `WM_NCCREATE`, cleared at `WM_NCDESTROY` |
| 11 | Important | Layered `EndDraw` failure ignored; a lost target is kept | confirmed | `D2DERR_RECREATE_TARGET` releases the surface; the ring resizes (recreates) before each draw |
| 12 | Important | Capture loss leaves a drag latched | confirmed | `WM_CAPTURECHANGED` ends the drag with an `up` |
| 13 | Important | The scroll container takes the wheel before a slider can | confirmed | Wheel applied at `endScroll` unless a child consumed it; sliders consume |
| 14 | Important | Ring commit uses the last timer sample | confirmed (≤ 11 ms stale) | The pointer is sampled at commit |
| 15 | Important | Unchecked `GetCursorPos` | confirmed | Checked; the pass is skipped on failure |
| 16 | Important | COM holder copyable, no move: NRVO-dependent double release | confirmed | Move-only holder |
| 17 | Important | `Impl` released after `CoUninitialize`; init HRESULTs ignored | confirmed | Scoped inside the apartment; init failures logged and the thread exits cleanly |
| 18 | Important | Text-selection queries can hit our own focused EDIT | confirmed | Skipped when the foreground window is ours |
| 19 | Important | A held button polls at 20 Hz even when no drag detector is on | confirmed | Polling while held only when drop / text / slider ticks are on |
| 20 | Important | `floor(NaN)` to int on non-finite slider bounds | confirmed | All three values and the range checked |
| 21 | Important | Scroll-end looked at once, 100 ms after the last notch; a glide can reach the end later | confirmed | A second look 500 ms after the first |
| 22 | Minor | Drag origin plus `dropFirst` suppress the first real target | confirmed | `dropFirst` removed |
| 23 | Minor | Selection check inspects the pointer 80 ms after release, not the clicked item | plausible | Deferred: UIA selection events would be the fix; the pointer rarely leaves the clicked tab within 80 ms |
| 24 | Minor | Popups taller than the client area are not scrollable | plausible | Deferred: the longest list (action kinds, 13 rows) is 350 DIP, the window minimum height is 560 |
| 25 | Minor | Pulse timestamps in `float` lose milliseconds after hours of uptime | confirmed | `double` |

Fidelity notes acted on: none required a change (pointer departure not closing the ring and the dead zone match the
Mac). Not supplied to the reviewer and therefore unverified by it: the input thread, router and action runner (reviewed
in round 2).

Missing tests the reviewer asked for: the toolkit interaction test now exists for dropdowns (open, select, dismiss).
The native EDIT, recorder, UIA replay and DPI/ring integration tests remain manual (screenshots and injected sweeps).
