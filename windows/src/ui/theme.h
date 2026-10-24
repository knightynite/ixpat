#pragma once
#include <cstdint>

#include "core/settings.h"

namespace ixpat::ui {

/// A colour with an alpha (0..1).
struct Tint {
    std::uint32_t rgb;
    float alpha;
};

/// Windows' accent and its ramp, as Settings > Personalization > Colors derives them.
struct AccentRamp {
    std::uint32_t light3, light2, light1, base, dark1, dark2, dark3;
};

/// The Fluent colour tokens this UI uses, resolved for light or dark and for the accent. Each field is named
/// after the WinUI resource it stands in for.
struct Palette {
    bool isDark;
    std::uint32_t accent;                       // AccentFillColorDefault: accent light 2 (dark theme) / dark 1 (light theme)
    std::uint32_t accentHover, accentPressed;   // AccentFillColorSecondary / Tertiary
    std::uint32_t accentText;                   // AccentTextFillColorPrimary: for text and the selection pill
    std::uint32_t textOnAccent;                 // TextOnAccentFillColorPrimary: white on light, black on dark
    std::uint32_t base;                         // SolidBackgroundFillColorBase: what Mica stands on, and the fallback without it
    Tint layer;                                 // LayerFillColorDefault: the content area over the backdrop
    Tint card;                                  // CardBackgroundFillColorDefault
    Tint cardStroke;                            // CardStrokeColorDefault
    Tint controlFill, controlFillHover, controlFillPressed;  // ControlFillColorDefault / Secondary / Tertiary
    Tint controlAlt;                            // ControlAltFillColorSecondary: a toggle's off track, a checkbox's empty box
    Tint controlStroke;                         // ControlStrokeColorDefault
    Tint controlEdge;                           // ControlElevationBorder: the one darker (light) or lighter (dark) edge
    bool edgeOnTop;                             // ...which sits at the top in dark and at the bottom in light
    Tint strongStroke;                          // ControlStrongStrokeColorDefault: a toggle's off border, a text box's bottom line
    Tint strongFill;                            // ControlStrongFillColorDefault: a slider's rest track
    std::uint32_t solidFill;                    // ControlSolidFillColorDefault: a slider's thumb
    Tint subtleHover, subtlePressed;            // SubtleFillColorSecondary / Tertiary: navigation rows, icon buttons
    std::uint32_t flyout;                       // the flyout surface (a solid stand-in for acrylic)
    Tint flyoutStroke;                          // SurfaceStrokeColorFlyout
    std::uint32_t text;                         // TextFillColorPrimary (baked onto the base for light)
    Tint text2, text3, textDisabled;            // TextFillColorSecondary / Tertiary / Disabled
    Tint hair;                                  // DividerStrokeColorDefault
    std::uint32_t mouseShell;
    Tint mouseEdge;
    std::uint32_t ok, warn, bad;                // SystemFillColorSuccess / Caution / Critical
    // Older names a few panes still use.
    Tint field;                                 // = controlFill
    Tint track;                                 // = strongFill
};

/// `accentHex` 0 means Windows' own accent (so does the first release's fixed blue, which nobody chose).
Palette resolve(AppTheme theme, bool systemDark, std::uint32_t accentHex);
AccentRamp accentRamp(std::uint32_t accentHex);
/// The accent as a fill on a dark or light surface (the ring is always dark).
std::uint32_t accentFill(std::uint32_t accentHex, bool dark);
/// HKCU\...\Themes\Personalize\AppsUseLightTheme == 0
bool systemPrefersDark();

}  // namespace ixpat::ui
