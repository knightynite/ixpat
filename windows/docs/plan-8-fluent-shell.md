# Plan 8: a Windows 11 app

**Goal:** the settings window looks and behaves like a Windows 11 app, not a port of a Mac window. One pass, no
new features.

## What it is

- **Backdrop.** Mica through the frame and the whole client: the frame is extended over the client, the Direct2D
  target paints with per-pixel alpha, and whatever is left transparent shows the backdrop. The system title bar
  keeps its tone from the theme. Without Mica (Windows 10, or DWM declining) the client is cleared to the solid base
  and the caption is painted to match.
- **Shell.** A 248 px navigation pane with the live mouse at its head (buttons light, pulses ripple), icon rows with
  the accent pill, the master switch and About at the foot. To the right, a content layer with its rounded corner,
  the page title in 28 px Display, and the page as a stack of settings cards (4 px corners, a hair of stroke, no
  shadows, no gloss). The title stays put; the cards scroll.
- **Controls.** The Fluent ones: 40 × 20 toggles with the bordered off state and the knob that grows on hover,
  ring-thumb sliders, 32 px buttons with the elevation edge, combo boxes, text boxes with the strong bottom line,
  a number box with stacked spin buttons, a selector bar with the accent pill under the chosen item, flyouts with
  the selection pill.
- **Type.** Segoe UI Variable: Display from 20 px up, Text below; body 14, captions 12, nothing smaller.
- **Colour.** The Fluent light and dark tokens, named after the WinUI resources they stand in for. The accent is
  Windows' own by default, read from the Explorer accent ramp: light 2 on dark, dark 1 on light, black on the
  accent in dark and white in light. A chosen hex in `settings.json` still works; the first release's fixed blue
  now means "Windows' own", since nobody chose it. The ring uses the same fill.
- **Copy.** No all-caps eyebrows, no middle-dot meta strings; page titles in sentence case.

## What changed underneath

- `theme.h`: the token palette and the accent ramp.
- `im.*`: the Fluent widgets, `navItem`, `layer`, `beginSections` / `sectionBreak` / `endSections` (a page as cards:
  `divider()` with full strength breaks the page into a new card, the half-strength line between rule rows stays a
  line), card heights measured by a layout pass right before every paint instead of the previous paint.
- `gfx_window.*`: Mica, the premultiplied target, theme and accent change messages, and the release of the mouse
  button before the capture, so a synthetic click is not released twice.
- `settings_window.*`: the pane, the page, the pages trimmed of their old in-card titles; the Live tile's readings
  moved to the top of Diagnostics; the native edit is a layered child so it stays opaque over Mica.
- Right-aligned controls were positioned with a width where an x was expected; it only worked while the content
  area began near the window edge.

## Checked

Every page captured in both themes at 150 % DPI (`shot.py` / `pages.py` in the session's scratch folder: the
window is raised with the Alt-tap trick, since a background process may not take the foreground; pages are switched
by posting clicks to the navigation rows).
