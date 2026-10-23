# iXpat for Windows, Plan 3: Action Ring and the Direct2D foundation

**Goal:** The Action Ring works on Windows exactly as on the Mac (open under the pointer by press or hold, sweep to a chamber with a haptic detent per crossing, click or release to run, right click or Escape to cancel), drawn with Direct2D in a layered click-through window. The rendering code it introduces (factory, DirectWrite text, the palette) is the base the settings window (Plan 4) builds on.

**Architecture:** `src/ui/d2d.h/.cpp` wraps the Direct2D 1.1 factory, DirectWrite factory and a `LayeredSurface` (32-bit DIB + `ID2D1DCRenderTarget` + `UpdateLayeredWindow`) for per-pixel-alpha windows. `src/ui/theme.h/.cpp` is `Theme.swift`'s palette. `src/ui/ring_window.h/.cpp` is `RadialMenuController` + `RadialMenuView`: a topmost, non-activating, click-through layered window polled at 90 Hz from a main-thread timer; geometry from `core/ring.h`. `src/ui/glyphs.h/.cpp` maps the Mac's SF Symbol names to Segoe Fluent Icons / Segoe MDL2 glyphs with a text fallback. The input thread gets the ring state (open, hold mode) so clicks, the chord press-intercept and Escape behave as on the Mac.

**Tech Stack:** as Plan 2 plus d2d1, dwrite, gdi32 (DIB section), the Segoe Fluent Icons font (Windows 11; Segoe MDL2 Assets fallback on Windows 10).

**Spec:** `windows/docs/design.md` sections 3.5 (ring), 3.6 (UI), 3.4 (ring geometry).

**Execution method chosen by the user:** native, independent review at the end (folded into the Plan 4 review if Plan 4 follows immediately).

## Global Constraints

- Plans 1 and 2 constraints hold. Rendering runs on the main thread only.
- The ring window never takes focus or receives mouse input: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST`, shown with `SW_SHOWNOACTIVATE`. The pointer is tracked by polling `GetCursorPos`.
- Ring size 360 px (logical, scaled by the monitor's DPI), dead zone 26 px, chamber 0 at 12 o'clock, clockwise, as `core/ring.h`.
- Colours: disc `0x151518` at 86% alpha, wedges white 6% / accent when selected, hub text white; accent from settings (`accentHex`).
- Glyph mapping is a table; unknown symbols draw the first letter of the item's title.

## Review Focus

1. Ring open across monitors with different DPI: the window must be sized for the monitor under the pointer and the dead zone / slot math must use logical pixels consistently (Task 3).
2. The pointer leaves the screen edge while the ring is open: `GetCursorPos` clamps; selection must still resolve (Task 3).
3. Ring opened in hold mode and the mouse disconnects: `deviceConnected(false)` closes it without committing (Task 4).
4. Escape pressed on a real keyboard while the ring is open cancels; Escape while the ring is closed passes through untouched (Task 4).
5. Rapid toggle (press, press) must not leave a stale timer or a stuck layered window (Task 3).

---

### Task 1: Direct2D foundation

**Files:** `windows/src/ui/d2d.h/.cpp`.
```cpp
namespace ixpat::ui {
struct Gfx {                                  // process-wide singletons, main thread
    static ID2D1Factory1* d2d(); static IDWriteFactory* dwrite();
};
class LayeredSurface {                        // a per-pixel-alpha surface for a WS_EX_LAYERED window
public:
    bool resize(int width, int height);       // 32-bit top-down DIB + ID2D1DCRenderTarget bound to it
    ID2D1RenderTarget* begin();               // BeginDraw, Clear(transparent)
    void end(HWND hwnd, POINT screenPos);     // EndDraw, UpdateLayeredWindow(ULW_ALPHA, premultiplied)
    int width() const; int height() const;
};
D2D1_COLOR_F color(std::uint32_t rgb, float alpha = 1);
}
```
Verify: a tiny debug command (`iXpat.exe --ring-demo`) opens the ring at the pointer for 3 s (added in Task 3). Commit `windows: direct2d foundation (factories, layered surface)`.

### Task 2: Theme and glyphs

**Files:** `windows/src/ui/theme.h/.cpp`, `windows/src/ui/glyphs.h/.cpp`; **Test:** `windows/tests/test_glyphs.cpp` (pure table lookup).
```cpp
namespace ixpat::ui {
struct Palette { bool isDark; std::uint32_t accent; std::uint32_t stageTop, stageBottom, bg, bar, panel, tile, text, text2, text3, hair, field, track, mouseShell, mouseEdge; float rowSel, rowHover; };
Palette resolve(AppTheme theme, bool systemDark, std::uint32_t accentHex);   // Theme.swift's table
bool systemPrefersDark();                                                    // HKCU ...\Themes\Personalize AppsUseLightTheme
struct AccentOption { const wchar_t* name; std::uint32_t hex; };
std::span<const AccentOption> accents();                                     // Blue, Purple, Pink, Orange, Green, Teal, Graphite
/// SF Symbol name -> Segoe Fluent Icons code point (0 = none). glyphs.cpp holds the table.
wchar_t glyphFor(std::string_view sfSymbol);
const wchar_t* iconFontFamily();                                             // "Segoe Fluent Icons" if installed, else "Segoe MDL2 Assets"
}
```
Tests: `glyphFor("magnifyingglass") == 0xE721`, `glyphFor("lock.fill") == 0xE72E`, unknown -> 0, `resolve(dark)` picks the dark colours and `resolve(system, false)` the light ones. Commit `windows: theme palette and glyph table`.

### Task 3: Ring window

**Files:** `windows/src/ui/ring_window.h/.cpp`.
```cpp
namespace ixpat::ui {
class RingWindow {
public:
    std::function<void()> onDetent, onOpen; std::function<void(const RingItem&)> onCommit;
    void create(HINSTANCE);                       // hidden layered window, created once
    void open(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex);
    void toggle(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex);
    void close(bool commit);
    void holdEnded();                             // hold mode: the action button was released -> commit
    bool isOpen() const; bool holdMode() const;
    void poll();                                  // main-thread timer at 90 Hz while open: GetCursorPos -> ringSelect -> redraw on change
};
}
```
Drawing (from `RadialMenuView`): disc radius `0.46 * size + 3`, wedges between `0.24 * size` and `0.46 * size` with 3 px gaps, selected wedge in the accent with a soft glow, glyph at the wedge's mid radius, hub shows the highlighted title (DirectWrite, 13 pt semibold) or nothing. Fade-in over 120 ms (alpha ramp in `poll`). Debug switch `--ring-demo` opens the default items for 3 s. Commit `windows: action ring window`.

### Task 4: Wiring

**Files:** `windows/src/app/main.cpp`, `windows/src/win/input_thread.h/.cpp`.
- Router sinks: `ringOpen(hold)` -> `ring.open(settings.effRingItems(), hold, accent)`, `ringToggle` -> `ring.toggle(...)`, `ringIsOpen`, `ringHoldEnded` -> `ring.holdEnded()`.
- Ring callbacks: `onDetent` -> `router.systemEvent(ringDetent)`, `onOpen` -> `ringOpen`, `onCommit` -> `ringCommit` then run the item's action after 80 ms (one-shot timer) so the ring is gone before a shortcut reaches the target app.
- Input thread: `setRingState(open, holdMode)`; left click while open -> `ringClick` (commit) and swallow; right click -> `ringCancel`; Escape (real keyboard, not injected) while open -> `ringCancel` and swallow; chord `pressIntercept` returns true when the ring is open in toggle mode (the press commits it), as `button.pressIntercept` on the Mac.
- `deviceConnected(false)` -> `ring.close(false)` via `ringHoldEnded` semantics (Mac: `ring.holdEnded()` on disconnect commits in hold mode; keep the Mac's behaviour: commit).
- Hold action `actionRing` (`ringOpenRequest`) opens the ring in toggle mode.
Verify manually with `IXPAT_ANY_MOUSE=1`: `--ring-demo`, then the real path with the action button once the mouse is connected. Commit `windows: action ring wired to the router and input thread`.

### Task 5: Review
Folded into Plan 4's independent review unless Plan 4 does not start the same day.
