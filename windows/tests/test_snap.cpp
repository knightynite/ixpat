#include <doctest/doctest.h>

#include <ostream>

#include "core/snap.h"

using ixpat::IRect;

namespace {
const IRect kWork{0, 0, 2560, 1400};  // a 2560x1440 monitor minus a 40 px taskbar
}

TEST_CASE("snap: a window that did not move is not a snap") {
    IRect before{100, 100, 900, 700};
    CHECK_FALSE(ixpat::looksSnapped(before, before, kWork));
    IRect jitter{102, 101, 902, 702};
    CHECK_FALSE(ixpat::looksSnapped(before, jitter, kWork));
}

TEST_CASE("snap: halves, quarters and the full work area count, with slack") {
    IRect before{100, 100, 900, 700};
    CHECK(ixpat::looksSnapped(before, IRect{0, 0, 1280, 1400}, kWork));        // left half
    CHECK(ixpat::looksSnapped(before, IRect{1280, 0, 2560, 1400}, kWork));     // right half
    CHECK(ixpat::looksSnapped(before, IRect{-7, -7, 1287, 1407}, kWork));      // left half with frame slack
    CHECK(ixpat::looksSnapped(before, IRect{1280, 700, 2560, 1400}, kWork));   // bottom-right quarter
    CHECK(ixpat::looksSnapped(before, IRect{0, 0, 2560, 1400}, kWork));        // maximised
    CHECK(ixpat::looksSnapped(before, IRect{0, 0, 2560, 700}, kWork));         // top half
}

TEST_CASE("snap: Windows 11 thirds count") {
    IRect before{100, 100, 900, 700};
    const int third = 2560 / 3;
    CHECK(ixpat::looksSnapped(before, IRect{0, 0, third, 1400}, kWork));
    CHECK(ixpat::looksSnapped(before, IRect{third, 0, 2 * third, 1400}, kWork));
    CHECK(ixpat::looksSnapped(before, IRect{2 * third, 0, 2560, 1400}, kWork));
    CHECK(ixpat::looksSnapped(before, IRect{third, 0, 2560, 1400}, kWork));  // the wide two-thirds
}

TEST_CASE("snap: an ordinary move or resize is not a snap") {
    IRect before{100, 100, 900, 700};
    CHECK_FALSE(ixpat::looksSnapped(before, IRect{300, 250, 1100, 850}, kWork));
    CHECK_FALSE(ixpat::looksSnapped(before, IRect{0, 0, 1000, 1400}, kWork));  // left edge but not a half (1000 vs 1280)
    CHECK_FALSE(ixpat::looksSnapped(before, IRect{0, 0, 1280, 1300}, kWork));  // too short for a half (100 px off)
}
