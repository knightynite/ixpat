#include "core/presets.h"

#include <array>

namespace ixpat {
namespace {

// Ordinals are the measured table: 3 HOVER 9 ms, 4 COLLIDE 26, 5 ALIGN 9, 6 STEP 11,
// 7 GROW 200, 8 SUCCESS 70, 9 ERROR 147, 10 CLICK 11.
constexpr HapticRule off(int o = 3) { return HapticRule{false, o, 1, std::nullopt}; }
constexpr HapticRule on(int o, int i) { return HapticRule{true, o, i, std::nullopt}; }
constexpr HapticRule on(int o, int i, HapticEcho e) { return HapticRule{true, o, i, e}; }
constexpr HapticRule r(bool enabled, int o, int i) { return HapticRule{enabled, o, i, std::nullopt}; }
constexpr HapticRule r(bool enabled, int o, int i, HapticEcho e) { return HapticRule{enabled, o, i, e}; }

const std::array<ClickFeel, 10> kClick{{
    {"Firmware only", "No extra pulse; the switch and the firmware's own click.", off(10), off(), off(7)},
    {"Subtle", "The lightest touch on press, nothing on release.", on(3, 1), off(), off(7)},
    {"Soft", "A rounded press and a feather on release.", on(5, 2), on(3, 1), off(7)},
    {"Crisp", "A sharp click with a faint release tick.", on(10, 2), on(3, 1), off(7)},
    {"Mechanical", "Click-clack: a firm press and a distinct release, like a microswitch.", on(10, 3), on(6, 2), off(7)},
    {"Deep", "A full-strength click with a low after-thud.", on(10, 4, {4, 1, 45}), on(5, 2), off(7)},
    {"Push back", "Resists on the way down, springs back on release.", on(4, 3), on(5, 3, {3, 1, 55}), off(7)},
    {"Double tap", "Two quick clicks per press.", on(10, 2, {10, 2, 65}), off(), off(7)},
    {"Hold & swell", "A crisp click, then a slow swell if you keep the button down.", on(10, 2), on(3, 1), on(7, 3)},
    {"Heavy hold", "A heavy press, a click on release, and a rising swell while held.", on(4, 4), on(6, 2),
     on(7, 4, {8, 2, 230})},
}};

const std::array<WheelFeel, 9> kWheel{{
    {"Classic ratchet", "A firm detent per notch that goes light when you spin fast; a flick free-spins silently.",
     r(true, 6, 2), 1, true, 10, true, r(false, 3, 1), 2},
    {"Fine", "Small, close ticks; the free-spin keeps a faint grain.", r(true, 3, 2), 1, true, 14, true, r(true, 3, 1), 2},
    {"Mechanical", "A click per notch, like a metal ratchet; lightens early when spinning.", r(true, 10, 3), 1, true, 8,
     true, r(false, 3, 1), 2},
    {"Heavy", "A thud per notch at any speed; no free-spin.", r(true, 4, 3), 1, false, 10, false, r(false, 3, 1), 2},
    {"Coarse", "A notch every second click of the wheel, with a coarse grain when free-spinning.", r(true, 5, 3), 2,
     false, 10, true, r(true, 6, 1), 3},
    {"Silk", "A whisper per notch and a continuous purr when the wheel slides.", r(true, 3, 1), 1, true, 6, true,
     r(true, 3, 1), 1},
    {"Slider", "Smooth slide feel: soft aligned bumps and an even texture, never a hard click.", r(true, 5, 2), 1, false,
     10, true, r(true, 5, 1), 1},
    {"Rumble", "Each notch doubles up into a short rumble.", r(true, 6, 3, {3, 2, 30}), 1, false, 10, true,
     r(true, 3, 1), 2},
    {"Silent", "No wheel pulses at all; free-spin still boosts distance.", r(false, 6, 2), 1, true, 10, true,
     r(false, 3, 1), 2},
}};

}  // namespace

std::span<const ClickFeel> clickFeels() { return kClick; }
std::span<const WheelFeel> wheelFeels() { return kWheel; }

void applyClickFeel(Settings& s, const ClickFeel& f, bool right) {
    s.baseRule(right ? HapticEvent::rightDown : HapticEvent::leftDown) = f.down;
    s.baseRule(right ? HapticEvent::rightUp : HapticEvent::leftUp) = f.up;
    s.baseRule(right ? HapticEvent::rightHold : HapticEvent::leftHold) = f.hold;
}

std::string_view clickFeelName(const Settings& s, bool right) {
    // The effective rules (a front-app profile may override them), as the Mac's picker reads them.
    const auto& down = s.rule(right ? HapticEvent::rightDown : HapticEvent::leftDown);
    const auto& up = s.rule(right ? HapticEvent::rightUp : HapticEvent::leftUp);
    const auto& hold = s.rule(right ? HapticEvent::rightHold : HapticEvent::leftHold);
    for (const auto& f : kClick) {
        if (f.down == down && f.up == up && f.hold == hold) return f.name;
    }
    return "Custom";
}

void applyWheelFeel(Settings& s, const WheelFeel& f) {
    s.baseRule(HapticEvent::scrollStep) = f.step;
    s.scrollStepEvery = f.every;
    s.ratchetDynamic = f.dynamic;
    s.ratchetLightRate = f.lightRate;
    s.freeSpin = f.freeSpin;
    s.baseRule(HapticEvent::freeSpinTexture) = f.texture;
    s.freeSpinTextureEvery = f.textureEvery;
}

std::string_view wheelFeelName(const Settings& s) {
    for (const auto& f : kWheel) {
        if (s.rule(HapticEvent::scrollStep) == f.step && s.scrollStepEvery == f.every &&
            s.ratchetDynamic == f.dynamic && s.ratchetLightRate == f.lightRate && s.freeSpin == f.freeSpin &&
            s.rule(HapticEvent::freeSpinTexture) == f.texture && s.freeSpinTextureEvery == f.textureEvery) {
            return f.name;
        }
    }
    return "Custom";
}

}  // namespace ixpat
