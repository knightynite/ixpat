#include "ui/theme.h"

#include <windows.h>

#include <algorithm>

namespace ixpat::ui {
namespace {

constexpr std::uint32_t kLegacyDefaultAccent = 0x0A84FF;  // the first release's fixed accent: now "Windows' own"

std::uint32_t blend(std::uint32_t rgb, std::uint32_t towards, float t) {
    auto ch = [&](int shift) {
        const float a = static_cast<float>((rgb >> shift) & 0xFF), b = static_cast<float>((towards >> shift) & 0xFF);
        return static_cast<std::uint32_t>(std::clamp(a + (b - a) * t, 0.0f, 255.0f)) << shift;
    };
    return ch(16) | ch(8) | ch(0);
}

/// Settings > Personalization > Colors keeps the chosen accent and its ramp in
/// HKCU\...\Explorer\Accent\AccentPalette: eight RGBA entries, light 3 to dark 3 and a complement.
bool readAccentPalette(AccentRamp& out) {
    BYTE bytes[32]{};
    DWORD size = sizeof bytes;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette", RRF_RT_REG_BINARY,
                     nullptr, bytes, &size) != ERROR_SUCCESS || size < 28) {
        return false;
    }
    auto rgb = [&](int i) {
        return (static_cast<std::uint32_t>(bytes[i * 4]) << 16) | (static_cast<std::uint32_t>(bytes[i * 4 + 1]) << 8) | bytes[i * 4 + 2];
    };
    out = {rgb(0), rgb(1), rgb(2), rgb(3), rgb(4), rgb(5), rgb(6)};
    return true;
}

AccentRamp rampFrom(std::uint32_t base) {
    return {blend(base, 0xFFFFFF, 0.6f), blend(base, 0xFFFFFF, 0.4f), blend(base, 0xFFFFFF, 0.2f), base,
            blend(base, 0x000000, 0.15f), blend(base, 0x000000, 0.3f), blend(base, 0x000000, 0.5f)};
}

AccentRamp systemRamp() {
    AccentRamp r{};
    if (readAccentPalette(r)) return r;
    DWORD abgr = 0, size = sizeof abgr;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &abgr, &size) == ERROR_SUCCESS) {
        return rampFrom(((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF));
    }
    return rampFrom(0x0078D4);  // Windows blue
}

}  // namespace

AccentRamp accentRamp(std::uint32_t accentHex) {
    const std::uint32_t hex = accentHex & 0xFFFFFF;
    if (hex == 0 || hex == kLegacyDefaultAccent) return systemRamp();
    return rampFrom(hex);
}

std::uint32_t accentFill(std::uint32_t accentHex, bool dark) {
    const AccentRamp r = accentRamp(accentHex);
    return dark ? r.light2 : r.dark1;
}

Palette resolve(AppTheme theme, bool systemDark, std::uint32_t accentHex) {
    const bool dark = theme == AppTheme::dark || (theme == AppTheme::system && systemDark);
    const AccentRamp ramp = accentRamp(accentHex);
    Palette p{};
    p.isDark = dark;
    if (dark) {
        p.accent = ramp.light2;
        p.accentHover = blend(ramp.light2, 0x202020, 0.1f);
        p.accentPressed = blend(ramp.light2, 0x202020, 0.2f);
        p.accentText = ramp.light3;
        p.textOnAccent = 0x000000;
        p.base = 0x202020;
        p.layer = {0x3A3A3A, 0.30f};
        p.card = {0xFFFFFF, 0.0512f};
        p.cardStroke = {0x000000, 0.10f};
        p.controlFill = {0xFFFFFF, 0.0605f};
        p.controlFillHover = {0xFFFFFF, 0.0837f};
        p.controlFillPressed = {0xFFFFFF, 0.0326f};
        p.controlAlt = {0x000000, 0.10f};
        p.controlStroke = {0xFFFFFF, 0.0698f};
        p.controlEdge = {0xFFFFFF, 0.093f};
        p.edgeOnTop = true;
        p.strongStroke = {0xFFFFFF, 0.6047f};
        p.strongFill = {0xFFFFFF, 0.5442f};
        p.solidFill = 0x454545;
        p.subtleHover = {0xFFFFFF, 0.0605f};
        p.subtlePressed = {0xFFFFFF, 0.0419f};
        p.flyout = 0x2C2C2C;
        p.flyoutStroke = {0x000000, 0.20f};
        p.text = 0xFFFFFF;
        p.text2 = {0xFFFFFF, 0.786f};
        p.text3 = {0xFFFFFF, 0.5442f};
        p.textDisabled = {0xFFFFFF, 0.3628f};
        p.hair = {0xFFFFFF, 0.0837f};
        p.editBackground = 0x353535;
        p.mouseShell = 0x3A3A3A;
        p.mouseEdge = {0xFFFFFF, 0.16f};
        p.ok = 0x6CCB5F;
        p.warn = 0xFCE100;
        p.bad = 0xFF99A4;
    } else {
        p.accent = ramp.dark1;
        p.accentHover = blend(ramp.dark1, 0xF3F3F3, 0.1f);
        p.accentPressed = blend(ramp.dark1, 0xF3F3F3, 0.2f);
        p.accentText = ramp.dark2;
        p.textOnAccent = 0xFFFFFF;
        p.base = 0xF3F3F3;
        p.layer = {0xFFFFFF, 0.50f};
        p.card = {0xFFFFFF, 0.70f};
        p.cardStroke = {0x000000, 0.0578f};
        p.controlFill = {0xFFFFFF, 0.70f};
        p.controlFillHover = {0xF9F9F9, 0.50f};
        p.controlFillPressed = {0xF9F9F9, 0.30f};
        p.controlAlt = {0x000000, 0.0241f};
        p.controlStroke = {0x000000, 0.0578f};
        p.controlEdge = {0x000000, 0.1622f};
        p.edgeOnTop = false;
        p.strongStroke = {0x000000, 0.4458f};
        p.strongFill = {0x000000, 0.4458f};
        p.solidFill = 0xFFFFFF;
        p.subtleHover = {0x000000, 0.0373f};
        p.subtlePressed = {0x000000, 0.0241f};
        p.flyout = 0xF9F9F9;
        p.flyoutStroke = {0x000000, 0.0578f};
        p.text = 0x1B1B1B;
        p.text2 = {0x000000, 0.6063f};
        p.text3 = {0x000000, 0.56f};  // Fluent's 0.4458 reads at 3.3:1 on the layer; this keeps 12 px copy above 4.5:1
        p.textDisabled = {0x000000, 0.3614f};
        p.hair = {0x000000, 0.0803f};
        p.editBackground = 0xFFFFFF;
        p.mouseShell = 0xF4F4F6;
        p.mouseEdge = {0x000000, 0.16f};
        p.ok = 0x0F7B0F;
        p.warn = 0x9D5D00;
        p.bad = 0xC42B1C;
    }
    // A chosen accent can be anything: the text on it follows its brightness (the Windows ramp keeps the Fluent rule).
    const std::uint32_t hex = accentHex & 0xFFFFFF;
    if (hex != 0 && hex != kLegacyDefaultAccent) {
        const double lum = 0.2126 * ((p.accent >> 16) & 0xFF) + 0.7152 * ((p.accent >> 8) & 0xFF) + 0.0722 * (p.accent & 0xFF);
        p.textOnAccent = lum > 150 ? 0x000000 : 0xFFFFFF;
    }
    p.field = p.controlFill;
    p.track = p.strongFill;
    return p;
}

bool systemPrefersDark() {
    DWORD value = 1, size = sizeof value;
    const LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return r == ERROR_SUCCESS && value == 0;
}

}  // namespace ixpat::ui
