# Plan 8 review (independent review, 2026-10-24) and the triage

Read-only review of the settings window: the toolkit, the window class with Mica, the native edit, the pages and
the theme. 24 findings; the ones that reach a user or correctness were fixed, the rest are recorded here as known
gaps.

## Fixed

| # | Finding | Action |
|---|---|---|
| 1 | Mica was taken as on when only the backdrop call succeeded | Both DWM calls must succeed; otherwise an ordinary frame, no backdrop, an opaque client |
| 2 | The native edit's font ignored a DPI change and the field's monospaced style | A font per DPI and style, owned by the window, replaced on `WM_DPICHANGED` before the old one goes |
| 3 | A commit read at most 2047 characters and wrote the setting with them | The whole text (`GetWindowTextLengthW`); a failed read leaves the setting alone |
| 4 | A page change while editing lost the commit's change notification | The commit notifies on its own when no field on the new page consumed it |
| 5 | The edit destroyed from outside left dangling state; `editTarget` outlived the edit | `WM_NCDESTROY` of the child clears the owner's state; commit and cancel clear everything |
| 6 | The change callback ran before the commit's state was cleared | The transaction's state goes first, then the value and the notification |
| 7 | `WM_NCDESTROY` skipped the default processing and the user-data reset | Both done with the handle preserved |
| 8 | A lost render target waited for the next unrelated event to repaint; a failed resize kept the target | Repaint asked after `EndPaint`; a failed resize drops the target |
| 9 | A half-built layered surface counted as ready; text code dereferenced a missing DirectWrite factory | Construction is all or nothing; no factory, no text |
| 10 | Losing capture synthesised a release (and could toggle); a right-button release counted | A cancel event ends a press without activating; releases are left-button only |
| 11 | A widget disabled or gone mid-press kept the active ownership | A release or cancel ends any press whatever the widget's state; the frame clears it at the end regardless |
| 12 | Right clicks and the flyout's padding selected rows | Rows are hit-tested as rectangles, left button only |
| 13 | A dropdown stayed open across a page change, swallowing the next click | Page changes clear popup state |
| 16 | The native edit ignored the page's clip when its field scrolled away | The edit is committed once its field leaves the viewport |
| 17 | Ring rows with a shell action overflowed the card at the default width | Field widths follow the room left in the row |
| 18 | The Apps page's editor broke the shared two-column card into new cards | Lines within the card instead |
| 19 | Light tertiary text at 3.3:1; white text on a bright custom accent | Light tertiary raised to 4.5:1; a custom accent's foreground follows its brightness |
| 20 | The edit's background was hard-coded and not repainted on a theme change | A palette token; the child repaints with the theme |
| 14 | A flyout taller than the window had unreachable rows | Capped to the window, scrolled by wheel and keyboard, with a rail |
| 15 | No keyboard navigation between the drawn controls | Tab order, the Fluent focus ring, Space/Enter, arrows, Home/End, keyboard flyouts |

## Known gaps (deferred)

| # | Finding | Why deferred |
|---|---|---|
| 21 | The Direct2D and DirectWrite factories live for the process | Bounded, released by the process |
| 22 | A slider keeps a partial wheel notch while the pointer is elsewhere | Harmless; the remainder is under one notch |
| 23, 24 | Implicit conversions, unused members, a tooltip parameter, mojibake | The build is clean under `/W4 /WX`; the mojibake was in the review's input, not the sources (the UTF-8 literals render correctly) |

Confirmed sound by the review: premultiplied alpha on both target types with straight-alpha brush colours; the
layered child over Mica (no `UpdateLayeredWindow` needed); the Windows 10 fallback; no per-frame layout or format
leak; DirectWrite sizes in DIPs across DPI changes; creation-time handle binding; edit completion without the
kill-focus recursion; id equality for clicks; wheel accumulation; outside-click dismissal and Escape; scroll
clamping; theme messages reaching `applyTheme()`; secondary text at 5.8:1 light and 9.7:1 dark on the solid layer;
bounded, type-checked registry reads with a fallback chain.
