# iXpat for Windows, Plan 4: Settings window

**Goal:** The settings window of the Mac app on Windows: header with the master switch, hero tile with the live schematic, live tile, pill tabs and the six panes (Haptics, Action Button, Action Ring, Apps, Pointer & Scroll, Diagnostics), plus the setup guide and About, all drawn with Direct2D in the Mac's visual language (Theme.swift palette, glass tiles, backdrop glows).

**Architecture:** `src/ui/gfx_window` is a base class for a Direct2D window (HwndRenderTarget, per-monitor DPI, paint on invalidate, input dispatch, 60 Hz animation timer on demand). `src/ui/im` is an immediate-mode toolkit: one `build()` function per window runs for painting and for every input event against the same layout, so widget state lives in `Settings` and a tiny `UiState` (hot/active/focused id, open dropdown, scroll offset, native edit control). `src/ui/settings_window` holds the panes; `src/ui/schematic` the mouse drawing; `src/ui/setup_window` and `src/ui/about_window` the two small windows. `src/app/main.cpp` opens the settings window on launch (unless `IXPAT_NO_WINDOW=1`), from the tray and from a second instance, and feeds it device/engine/button state.

**Tech Stack:** as Plan 3 (d2d1, dwrite), plus dwmapi (dark title bar), comctl32/uxtheme (the native edit control while a text field is being edited).

**Spec:** `windows/docs/design.md` 3.6.

**Execution method chosen by the user:** native; independent review of Plans 3 + 4 together at the end.

## Global Constraints

- Plans 1-3 constraints hold; all UI on the main thread; no UI work from hooks or device threads.
- Widget ids are stable strings; every control that changes `Settings` calls `markDirty` (debounced save) and `settingsChanged()` (push to the threads) through one callback.
- The window is a standard framed window (dark title bar via DWM) sized 860x700 logical, resizable down to 760x560; the content scrolls. The frameless/Mica look is deferred (ruling, see ledger).
- Text is DirectWrite "Segoe UI"; sizes and weights follow MainWindow.swift (10/11/12/13/14/18 pt, medium/semibold).
- Keyboard: Tab is not implemented in this plan (mouse-first, like the Mac window); Escape closes an open dropdown, Enter commits a text field.

## Review Focus

1. 150% and 200% DPI: hit rects and drawing must agree (both in DIPs with the render target's DPI set) (Task 1).
2. A dropdown opened near the bottom edge must flip upwards and never leave the window (Task 1).
3. Editing a text field, then clicking a toggle: the edit must commit first, then the toggle must act (Task 1).
4. The device disconnects while the window is open: the hero tile, Diagnostics and the test buttons must update without a repaint being forced by the user (Task 5).
5. Resizing the window to its minimum: no control may overlap or be clipped unreadably (Task 3).

---

### Task 1: Direct2D window base and the immediate-mode toolkit
Files: `src/ui/gfx_window.h/.cpp`, `src/ui/im.h/.cpp`, `--ui-demo` switch in `main.cpp` that opens a demo window exercising every widget. Verified by a CAPTUREBLT screenshot at 150% DPI. Commit `windows: direct2d window base and immediate-mode toolkit`.

### Task 2: Schematic and hero/live tiles
Files: `src/ui/schematic.h/.cpp` (MouseBodyShape bezier, button fills from the raw button mask, wheel notches, action button glow, pulse rings animated 600 ms), header bar (wordmark, tagline, master switch), hero tile (connection pill, battery badge, firmware), live tile (pulse count, last event, last pulse). Commit `windows: settings window shell, schematic, hero and live tiles`.

### Task 3: Panes
Haptics (master, strength segmented, feel presets, 33 rule rows with waveform dropdown, intensity segmented, test button, steppers for scrollStep/freeSpinTexture/leftHold, echo chip), Action Button (chord explanation, last gesture, three action rows with ActionPicker: kind dropdown + detail: choose app file dialog / record shortcut / shell text field, gesturesOnHold, gesture pane, hold action section (Windows: hold action dropdown + delay slider), swallow chords), Action Ring (open now, ring editor rows: glyph, title, symbol, action picker, remove/add/reset), Apps (profile list from running top-level windows' processes + choose .exe, profile editor with tri-state pickers, feel pickers, own gestures, own ring), Pointer & Scroll (manage pointer, speed, no accel, scroll lines, reverse, wheel feel sliders, ratchet, free-spin, link to Windows mouse settings), Diagnostics (device grid, hooks/raw-input status, bridge section with token copy/new/port, raw report log, copy report, read table). Commit per pane.

### Task 4: Setup guide and About
`src/ui/setup_window` (pair the mouse, confirm the haptics collection opens, try a pulse; "Don't show again"), `src/ui/about_window` (icon, wordmark, version/arch, links, update check toggle + Check now placeholder until Plan 6). Commit `windows: setup guide and about window`.

### Task 5: Wiring
`main.cpp`: open on launch / tray click / WM_APP_SHOW / menu; feed `DeviceInfo`, engine stats, button pressed state, raw report log lines (ring buffer of 150) and the InputThread status; settings edits -> `markDirty` + `settingsChanged`; test buttons -> engine.preview / router.run / ring toggle. Commit `windows: settings window wired`.

### Task 6: Review
independent review of Plans 3 + 4 (ui + wiring diff).
