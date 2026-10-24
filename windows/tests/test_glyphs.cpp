#include <doctest/doctest.h>

#include <ostream>

#include "ui/glyphs.h"
#include "ui/theme.h"

TEST_CASE("glyphs: the Mac's SF Symbol names map to Segoe icon code points") {
    CHECK(ixpat::ui::glyphFor("magnifyingglass") == 0xE721);
    CHECK(ixpat::ui::glyphFor("lock.fill") == 0xE72E);
    CHECK(ixpat::ui::glyphFor("doc.on.doc") == 0xE8C8);
    CHECK(ixpat::ui::glyphFor("doc.on.clipboard") == 0xE77F);
    CHECK(ixpat::ui::glyphFor("rectangle.3.group") == 0xE7C4);
    CHECK(ixpat::ui::glyphFor("camera.viewfinder") == 0xE722);
    CHECK(ixpat::ui::glyphFor("playpause.fill") == 0xE768);
    CHECK(ixpat::ui::glyphFor("menubar.dock.rectangle") == 0xE7F7);
    CHECK(ixpat::ui::glyphFor("not.a.symbol") == 0);
    CHECK(ixpat::ui::glyphFor("") == 0);
}

TEST_CASE("theme: the palette follows the theme choice; a chosen accent is lighter on dark and darker on light") {
    auto dark = ixpat::ui::resolve(ixpat::AppTheme::dark, false, 0x0078D4);
    CHECK(dark.isDark);
    CHECK(dark.base == 0x202020u);
    CHECK(dark.text == 0xFFFFFFu);
    CHECK(dark.textOnAccent == 0x000000u);
    CHECK(dark.accent == ixpat::ui::accentRamp(0x0078D4).light2);
    auto light = ixpat::ui::resolve(ixpat::AppTheme::light, true, 0x0078D4);
    CHECK_FALSE(light.isDark);
    CHECK(light.base == 0xF3F3F3u);
    CHECK(light.textOnAccent == 0xFFFFFFu);
    CHECK(light.accent == ixpat::ui::accentRamp(0x0078D4).dark1);
    CHECK(ixpat::ui::resolve(ixpat::AppTheme::system, true, 0).isDark);
    CHECK_FALSE(ixpat::ui::resolve(ixpat::AppTheme::system, false, 0).isDark);
}

TEST_CASE("theme: the ramp of a chosen accent keeps its base and orders light to dark") {
    const auto r = ixpat::ui::accentRamp(0x0078D4);
    CHECK(r.base == 0x0078D4u);
    auto lum = [](std::uint32_t c) { return ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF); };
    CHECK(lum(r.light3) > lum(r.light2));
    CHECK(lum(r.light2) > lum(r.light1));
    CHECK(lum(r.light1) > lum(r.base));
    CHECK(lum(r.base) > lum(r.dark1));
    CHECK(lum(r.dark1) > lum(r.dark2));
    CHECK(lum(r.dark2) > lum(r.dark3));
    // 0 and the first release's fixed blue both mean Windows' own accent: the same ramp either way.
    CHECK(ixpat::ui::accentRamp(0).base == ixpat::ui::accentRamp(0x0A84FF).base);
    CHECK(ixpat::ui::accentFill(0x0078D4, true) == r.light2);
    CHECK(ixpat::ui::accentFill(0x0078D4, false) == r.dark1);
}
