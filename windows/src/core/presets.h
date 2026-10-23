#pragma once
#include <span>
#include <string_view>

#include "core/events.h"
#include "core/settings.h"

namespace ixpat {

/// A preset writes several rules at once; every row stays editable afterwards and the picker shows
/// "Custom" until the rules match a preset again. Tables are Settings.swift's `ClickFeel` / `WheelFeel`.
struct ClickFeel {
    std::string_view name;
    std::string_view blurb;
    HapticRule down, up, hold;
};

struct WheelFeel {
    std::string_view name;
    std::string_view blurb;
    HapticRule step;
    int every;
    bool dynamic;
    double lightRate;
    bool freeSpin;
    HapticRule texture;
    int textureEvery;
};

std::span<const ClickFeel> clickFeels();  // 10, Swift order
std::span<const WheelFeel> wheelFeels();  // 9, Swift order

void applyClickFeel(Settings& s, const ClickFeel& f, bool right);
std::string_view clickFeelName(const Settings& s, bool right);  // "Custom" when nothing matches
void applyWheelFeel(Settings& s, const WheelFeel& f);
std::string_view wheelFeelName(const Settings& s);

}  // namespace ixpat
