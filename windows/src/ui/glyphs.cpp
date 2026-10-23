#include "ui/glyphs.h"

#include <windows.h>

#include <array>

namespace ixpat::ui {
namespace {

struct Map {
    std::string_view sf;
    wchar_t glyph;
};
// Segoe Fluent Icons / MDL2 Assets code points.
constexpr std::array<Map, 40> kGlyphs{{
    {"rectangle.3.group", 0xE7C4},        // TaskView
    {"magnifyingglass", 0xE721},          // Search
    {"camera.viewfinder", 0xE722},        // Camera
    {"camera", 0xE722},
    {"playpause.fill", 0xE768},           // Play
    {"play.fill", 0xE768},
    {"pause.fill", 0xE769},
    {"menubar.dock.rectangle", 0xE7F7},   // Devices (show desktop)
    {"lock.fill", 0xE72E},                // Lock
    {"lock", 0xE72E},
    {"doc.on.doc", 0xE8C8},               // Copy
    {"doc.on.clipboard", 0xE77F},         // Paste
    {"scissors", 0xE8C6},                 // Cut
    {"arrow.uturn.backward", 0xE7A7},     // Undo
    {"arrow.uturn.forward", 0xE7A6},      // Redo
    {"speaker.wave.2.fill", 0xE767},      // Volume
    {"speaker.slash.fill", 0xE74F},       // Mute
    {"forward.fill", 0xE893},             // Next
    {"backward.fill", 0xE892},            // Previous
    {"sun.max.fill", 0xE706},             // Brightness
    {"keyboard", 0xE765},
    {"terminal", 0xE756},                 // CommandPrompt
    {"app.fill", 0xE71D},                 // AllApps
    {"square.grid.2x2", 0xE80A},          // ViewAll
    {"gear", 0xE713},                     // Settings
    {"gearshape", 0xE713},
    {"power", 0xE7E8},
    {"moon.fill", 0xE708},                // QuietHours
    {"display", 0xE7F4},                  // TVMonitor
    {"folder", 0xE8B7},
    {"trash", 0xE74D},
    {"star.fill", 0xE735},
    {"bell", 0xEA8F},
    {"globe", 0xE774},
    {"envelope", 0xE715},
    {"calendar", 0xE787},
    {"mic", 0xE720},
    {"bolt.fill", 0xE945},
    {"heart.fill", 0xEB52},
    {"house", 0xE80F},
}};

bool fontInstalled(const wchar_t* family) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, family, _TRUNCATE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(
        dc, &lf,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            *reinterpret_cast<bool*>(p) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

}  // namespace

wchar_t glyphFor(std::string_view sfSymbol) {
    for (const auto& m : kGlyphs) {
        if (m.sf == sfSymbol) return m.glyph;
    }
    return 0;
}

const wchar_t* iconFontFamily() {
    static const wchar_t* family = fontInstalled(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return family;
}

}  // namespace ixpat::ui
