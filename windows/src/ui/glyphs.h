#pragma once
#include <string_view>

namespace ixpat::ui {

/// The Mac stores SF Symbol names in settings.json (`RingItem.symbol`). On Windows they are drawn with
/// Segoe Fluent Icons (Windows 11) or Segoe MDL2 Assets (Windows 10), which share code points.
/// Returns 0 for an unknown name; callers then draw the item's first letter instead.
wchar_t glyphFor(std::string_view sfSymbol);

/// "Segoe Fluent Icons" when installed, else "Segoe MDL2 Assets".
const wchar_t* iconFontFamily();

}  // namespace ixpat::ui
