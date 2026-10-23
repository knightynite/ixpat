#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <string>

#include "core/ring.h"
#include "core/version.h"

TEST_CASE("ring: chamber 0 sits at 12 o'clock and chambers go clockwise") {
    const double pi = 3.14159265358979323846;
    CHECK(ixpat::ringSlotIndex(-pi / 2, 8) == 0);  // straight up (y down coordinates)
    CHECK(ixpat::ringSlotIndex(0, 8) == 2);        // right
    CHECK(ixpat::ringSlotIndex(pi / 2, 8) == 4);   // down
    CHECK(ixpat::ringSlotIndex(pi, 8) == 6);       // left
    CHECK(ixpat::ringSlotIndex(-pi, 8) == 6);
    CHECK(ixpat::ringSlotIndex(-pi / 2 + 0.3, 8) == 0);  // within half a slice
    CHECK(ixpat::ringSlotIndex(-pi / 2 + 0.5, 8) == 1);
    CHECK(ixpat::ringSlotIndex(-pi / 2 - 0.5, 8) == 7);
    CHECK(ixpat::ringSlotIndex(0, 4) == 1);
    CHECK(ixpat::ringSlotIndex(0, 1) == 0);
    CHECK(ixpat::kRingDeadZone == 26.0);
    CHECK(ixpat::kRingSize == 360);
}

TEST_CASE("ring: a direction resolves to a slot or the dead zone") {
    auto s = ixpat::ringSelect(0, -100, 8);
    REQUIRE(s.has_value());
    CHECK(*s == 0);
    CHECK_FALSE(ixpat::ringSelect(10, -10, 8).has_value());  // inside the 26 px dead zone
    CHECK(ixpat::ringSelect(100, 0, 8) == 2);
    CHECK_FALSE(ixpat::ringSelect(100, 0, 0).has_value());   // no chambers
}

TEST_CASE("version: numeric dotted compare") {
    CHECK(ixpat::isNewerVersion("0.2.0", "0.1.1"));
    CHECK_FALSE(ixpat::isNewerVersion("0.1.1", "0.1.1"));
    CHECK_FALSE(ixpat::isNewerVersion("0.1.0", "0.1.1"));
    CHECK(ixpat::isNewerVersion("1.0", "0.9.9"));
    CHECK(ixpat::isNewerVersion("0.1.1.1", "0.1.1"));
    CHECK_FALSE(ixpat::isNewerVersion("0.1.1", "0.1.1.0"));
    CHECK(ixpat::isNewerVersion("0.10.0", "0.9.0"));
    CHECK_FALSE(ixpat::isNewerVersion("garbage", "0.1.0"));  // non-numeric parts count as 0
}

TEST_CASE("version: release tags lose their prefix") {
    CHECK(ixpat::stripTagPrefix("win-v0.2.0") == "0.2.0");
    CHECK(ixpat::stripTagPrefix("v0.1.1") == "0.1.1");
    CHECK(ixpat::stripTagPrefix("0.1.1") == "0.1.1");
    CHECK(ixpat::stripTagPrefix("") == "");
}
