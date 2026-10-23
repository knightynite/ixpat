#include "ui/theme.h"

#include <windows.h>

#include <array>

namespace ixpat::ui {
namespace {

constexpr std::array<AccentOption, 7> kAccents{{
    {L"Blue", 0x0A84FF}, {L"Purple", 0xBF5AF2}, {L"Pink", 0xFF375F}, {L"Orange", 0xFF9F0A},
    {L"Green", 0x30D158}, {L"Teal", 0x40C8E0}, {L"Graphite", 0x98989D},
}};

}  // namespace

Palette resolve(AppTheme theme, bool systemDark, std::uint32_t accentHex) {
    const bool dark = theme == AppTheme::dark || (theme == AppTheme::system && systemDark);
    Palette p{};
    p.isDark = dark;
    p.accent = accentHex & 0xFFFFFF;
    if (dark) {
        p.stageTop = 0x3A3A45;
        p.stageBottom = 0x101013;
        p.bg = 0x1F1F22;
        p.bar = 0x2A2A2D;
        p.panel = 0x2C2C2E;
        p.tile = 0x2C2C2E;
        p.text = 0xFFFFFF;
        p.mouseShell = 0x3B3B41;
        p.text2 = {0xEBEBF5, 0.60f};
        p.text3 = {0xEBEBF5, 0.32f};
        p.hair = {0xFFFFFF, 0.09f};
        p.field = {0x787880, 0.26f};
        p.track = {0x787880, 0.34f};
        p.rowSel = {0xFFFFFF, 0.07f};
        p.rowHover = {0xFFFFFF, 0.05f};
        p.mouseEdge = {0xFFFFFF, 0.18f};
    } else {
        p.stageTop = 0xDFE3EA;
        p.stageBottom = 0xB7BCC7;
        p.bg = 0xECECEE;
        p.bar = 0xF7F7F9;
        p.panel = 0xFFFFFF;
        p.tile = 0xECECED;
        p.text = 0x1D1D1F;
        p.mouseShell = 0xF4F4F6;
        p.text2 = {0x3C3C43, 0.60f};
        p.text3 = {0x3C3C43, 0.30f};
        p.hair = {0x000000, 0.09f};
        p.field = {0x787880, 0.16f};
        p.track = {0x787880, 0.26f};
        p.rowSel = {0x000000, 0.055f};
        p.rowHover = {0x000000, 0.035f};
        p.mouseEdge = {0x000000, 0.16f};
    }
    return p;
}

bool systemPrefersDark() {
    DWORD value = 1, size = sizeof value;
    const LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return r == ERROR_SUCCESS && value == 0;
}

std::span<const AccentOption> accents() { return kAccents; }

}  // namespace ixpat::ui
