#pragma once
#include <cstdint>
#include <span>

#include "core/settings.h"

namespace ixpat::ui {

/// A colour with an alpha (0..1).
struct Tint {
    std::uint32_t rgb;
    float alpha;
};

/// Resolved colours for the current theme + accent (Theme.swift's Palette).
struct Palette {
    bool isDark;
    std::uint32_t accent;
    std::uint32_t stageTop, stageBottom, bg, bar, panel, tile, text, mouseShell;
    Tint text2, text3, hair, field, track, rowSel, rowHover, mouseEdge;
};

Palette resolve(AppTheme theme, bool systemDark, std::uint32_t accentHex);
/// HKCU\...\Themes\Personalize\AppsUseLightTheme == 0
bool systemPrefersDark();

struct AccentOption {
    const wchar_t* name;
    std::uint32_t hex;
};
std::span<const AccentOption> accents();  // Blue, Purple, Pink, Orange, Green, Teal, Graphite

}  // namespace ixpat::ui
