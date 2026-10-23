# iXpat for Windows, Plan 5: UI Automation ticks and cursor-shape hover

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Mac's accessibility-driven pulses on Windows: menu item highlights, control hover, drop targets, text selection by word and line, slider detents, tab/list selection changes, overscroll bumps, and cursor-shape hover.

**Architecture:** `src/win/uia_watch.h/.cpp` runs one MTA thread with an `IUIAutomation` instance, polling every 50 ms while the pointer moved recently (or once after a click / wheel notch), using `ElementFromPointBuildCache` with a cache request so each poll is one cross-process call. Ticks are posted to main as `WM_APP_UIA` with the `HapticEvent` in `wParam`; main forwards them through the router (rules, profiles, master gates). Config flags (which ticks are on) are pushed from main under a lock. Cursor-shape hover runs on main from a 50 ms timer (`GetCursorInfo`), as the Mac's `HoverWatcher` runs on its main thread.

**Tech Stack:** uiautomationcore, ole32.

**Spec:** `windows/docs/design.md` 3.5 (the macOS → Windows table).

## Global Constraints
- The UIA thread never touches app state; it posts. It skips our own process's windows (`WindowFromPoint` pid check), like the Mac's `ownFrames`.
- Polls stop entirely when no tick is enabled or the pointer is idle (> 0.5 s), so an idle PC costs nothing.
- Pacing from the Mac: control hover 60 ms, drop 80 ms, word 90 ms, line 50 ms, slider 40 ms, selection 100 ms, scroll end 150 ms.

## Tasks
1. `UiaWatch` (thread, config, polling, the seven detectors) + `WM_APP_UIA` wiring, Diagnostics counters. Commit `windows: ui automation ticks`.
2. Cursor-shape hover on main (`hoverAffordance`, `dropTarget` from the drag cursor's badge). Commit `windows: cursor-shape hover ticks`.
3. Smoke: injected pointer sweeps over the taskbar and a menu, counts in Diagnostics and debug.log.
