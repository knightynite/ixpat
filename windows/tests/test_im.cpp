#include <doctest/doctest.h>

#include <span>
#include <string>
#include <vector>

#include "ui/im.h"
#include "ui/theme.h"

using namespace ixpat::ui;

namespace {

/// A headless settings-style pane: one dropdown whose index is a per-frame local mapped back into `value`
/// after the frame, exactly like ruleRow / actionPicker do. Each input pass is followed by one empty pass, as
/// the window does when the toolkit asks for a rerun.
struct DropdownRig {
    UiState st;
    Palette pal = resolve(ixpat::AppTheme::dark, true, 0x0A84FF);
    int value = 0;
    int changedCalls = 0;
    std::vector<std::wstring> options{L"A", L"B", L"C"};

    DropdownRig() { st.onChanged = [this] { ++changedCalls; }; }

    void one(const UiInput& in) {
        Frame f(nullptr, pal, in, st, 1.0f, 600, 400);
        f.begin({0, 0, 600, 400});
        int index = value;  // per-frame local, dead after this function returns
        const int before = index;
        f.dropdown("d", index, options, 150);
        f.finish();
        if (index != before) value = index;
    }
    void pass(const UiInput& in) {
        one(in);
        UiInput none;
        one(none);
    }
};

UiInput at(UiInput::Kind k, float x, float y) {
    UiInput in;
    in.kind = k;
    in.x = x;
    in.y = y;
    return in;
}

}  // namespace

TEST_CASE("im: the click that hits a dropdown opens it and leaves it open") {
    DropdownRig r;
    r.pass(at(UiInput::Kind::down, 30, 12));  // the box is the first 150 x 32 DIPs of the pane
    CHECK(r.st.openDropdown == "d");
}

TEST_CASE("im: a popup row click lands in the caller's value and reports exactly one change") {
    DropdownRig r;
    r.pass(at(UiInput::Kind::down, 30, 12));
    REQUIRE(r.st.openDropdown == "d");
    // popup: top = anchor bottom (32) + 4, 4 DIP pad, 36 DIP rows -> row 2 ("C") is centred at 36 + 4 + 72 + 18
    r.pass(at(UiInput::Kind::down, 30, 36 + 4 + 72 + 18));
    CHECK(r.value == 2);
    CHECK(r.st.openDropdown.empty());
    CHECK(r.changedCalls == 1);
}

TEST_CASE("im: a click outside an open popup closes it, changes nothing and does not reach the dropdown") {
    DropdownRig r;
    r.pass(at(UiInput::Kind::down, 30, 12));
    REQUIRE(r.st.openDropdown == "d");
    r.pass(at(UiInput::Kind::down, 400, 300));
    CHECK(r.st.openDropdown.empty());
    CHECK(r.value == 0);
    CHECK(r.changedCalls == 0);
    r.pass(at(UiInput::Kind::down, 30, 12));  // a fresh click on the box opens it again
    CHECK(r.st.openDropdown == "d");
}
