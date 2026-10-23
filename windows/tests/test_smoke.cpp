#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/clock.h"

TEST_CASE("toolchain: json parses") {
    CHECK(nlohmann::json::parse("{\"a\":1}")["a"] == 1);
}

TEST_CASE("toolchain: clock interface is implementable") {
    struct Fixed : ixpat::MonotonicClock {
        ixpat::Seconds now() const override { return 42.0; }
    } c;
    CHECK(c.now() == 42.0);
}
