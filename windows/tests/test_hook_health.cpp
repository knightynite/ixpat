#include <doctest/doctest.h>

#include "core/hook_health.h"

TEST_CASE("hook health: recover even when no probe has ever reached the hook") {
    ixpat::HookHealth h;
    CHECK_FALSE(h.observe(false, 1.0));
    CHECK(h.observe(false, 1.5));
}

TEST_CASE("hook health: one missed reply is not a failure and a reply clears it") {
    ixpat::HookHealth h;
    CHECK_FALSE(h.observe(false, 1.0));
    CHECK_FALSE(h.observe(true, 1.5));
    CHECK_FALSE(h.observe(false, 2.0));
    CHECK(h.observe(false, 2.5));
}

TEST_CASE("hook health: persistent failures back off and still recover later in the session") {
    ixpat::HookHealth h;
    int repairs = 0;
    for (int i = 0; i < 2400; ++i) {
        if (h.observe(false, i * 0.25)) ++repairs;
    }
    CHECK(repairs > 10);  // ten minutes of failures never switch recovery off
    CHECK(repairs < 30);  // nor do they create a reinstall storm
    CHECK_FALSE(h.observe(true, 630.0));
    CHECK_FALSE(h.observe(false, 631.0));
    CHECK(h.observe(false, 631.5));
    CHECK_FALSE(h.observe(false, 632.0));
    CHECK_FALSE(h.observe(false, 632.5));
    CHECK(h.observe(false, 633.5));  // healthy replies restored the short retry delay
}
