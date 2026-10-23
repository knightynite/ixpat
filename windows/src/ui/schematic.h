#pragma once
#include <cstdint>
#include <vector>

#include "ui/im.h"

namespace ixpat::ui {

/// Live schematic state fed by the app (MouseSchematic.swift).
struct SchematicState {
    bool connected = false;
    std::uint8_t buttons = 0;     // bit0 L, bit1 R, bit2 wheel
    int wheelTotal = 0;           // notches, drives the wheel texture phase
    bool actionPressed = false;   // the action button's chord is down
    float dx = 0, dy = 0;         // last motion, for the subtle body nudge
    std::vector<float> pulseAges; // seconds since each pulse ring started (owner trims past 0.7 s)
};

/// Top-down outline of the Surface Mouse 2nd Edition with live button fills, wheel and action button,
/// and the pulse rings that ripple out of the body on every haptic. Draws inside `area`.
void drawSchematic(Frame& f, const Rect& area, const SchematicState& s);

}  // namespace ixpat::ui
