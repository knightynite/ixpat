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

TEST_CASE("theme: the palette follows the theme choice and the accent") {
    auto dark = ixpat::ui::resolve(ixpat::AppTheme::dark, false, 0x0A84FF);
    CHECK(dark.isDark);
    CHECK(dark.accent == 0x0A84FFu);
    CHECK(dark.bg == 0x1F1F22u);
    CHECK(dark.text == 0xFFFFFFu);
    auto light = ixpat::ui::resolve(ixpat::AppTheme::light, true, 0xBF5AF2);
    CHECK_FALSE(light.isDark);
    CHECK(light.accent == 0xBF5AF2u);
    CHECK(light.bg == 0xECECEEu);
    CHECK(light.text == 0x1D1D1Fu);
    CHECK(ixpat::ui::resolve(ixpat::AppTheme::system, true, 0).isDark);
    CHECK_FALSE(ixpat::ui::resolve(ixpat::AppTheme::system, false, 0).isDark);
    REQUIRE(ixpat::ui::accents().size() == 7);
    CHECK(ixpat::ui::accents()[0].hex == 0x0A84FFu);
    CHECK(ixpat::ui::accents()[6].hex == 0x98989Du);
}
