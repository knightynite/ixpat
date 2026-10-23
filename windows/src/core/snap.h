#pragma once
#include <array>
#include <cstdlib>

namespace ixpat {

/// Integer rectangle in screen pixels (left/top inclusive, right/bottom exclusive).
struct IRect {
    int left, top, right, bottom;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

/// Did a drag end with the window snapped to a tiling slot of its monitor's work area? The slots are
/// the Mac's (full, halves, quarters) plus Windows 11's thirds and two-thirds; slack is 12 px on the
/// origin and 24 px on the size, as in EventMonitor.checkSnap. A window that did not move is never a snap.
inline bool looksSnapped(const IRect& before, const IRect& now, const IRect& work) {
    if (std::abs(before.left - now.left) < 4 && std::abs(before.top - now.top) < 4 &&
        std::abs(before.width() - now.width()) < 4 && std::abs(before.height() - now.height()) < 4) {
        return false;
    }
    const int w = work.width(), h = work.height();
    const int halfW = w / 2, halfH = h / 2, third = w / 3;
    const int x0 = work.left, y0 = work.top, xm = work.left + halfW, ym = work.top + halfH;
    const std::array<IRect, 14> slots{{
        work,
        {x0, y0, x0 + halfW, work.bottom},                     // left half
        {xm, y0, work.right, work.bottom},                     // right half
        {x0, y0, work.right, y0 + halfH},                      // top half
        {x0, ym, work.right, work.bottom},                     // bottom half
        {x0, y0, x0 + halfW, y0 + halfH},                      // quarters
        {xm, y0, work.right, y0 + halfH},
        {x0, ym, x0 + halfW, work.bottom},
        {xm, ym, work.right, work.bottom},
        {x0, y0, x0 + third, work.bottom},                     // thirds
        {x0 + third, y0, x0 + 2 * third, work.bottom},
        {x0 + 2 * third, y0, work.right, work.bottom},
        {x0, y0, x0 + 2 * third, work.bottom},                 // two-thirds
        {x0 + third, y0, work.right, work.bottom},
    }};
    for (const auto& s : slots) {
        if (std::abs(s.left - now.left) < 12 && std::abs(s.top - now.top) < 12 && std::abs(s.width() - now.width()) < 24 &&
            std::abs(s.height() - now.height()) < 24) {
            return true;
        }
    }
    return false;
}

}  // namespace ixpat
