#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <vector>

#include "core/haptic_table.h"

namespace {

// Feature report 0x42 as the device returns it: [id][50 x u16 durations][50 x u16 usages] = 201 bytes.
std::vector<std::uint8_t> featureFromTable(std::span<const ixpat::Waveform> table) {
    std::vector<std::uint8_t> raw(201, 0);
    raw[0] = 0x42;
    for (const auto& w : table) {
        const int i = w.ordinal - 3;
        raw[1 + 2 * i] = static_cast<std::uint8_t>(w.durationMs & 0xFF);
        raw[2 + 2 * i] = static_cast<std::uint8_t>(w.durationMs >> 8);
        raw[101 + 2 * i] = static_cast<std::uint8_t>(w.usage & 0xFF);
        raw[102 + 2 * i] = static_cast<std::uint8_t>(w.usage >> 8);
    }
    return raw;
}

}  // namespace

TEST_CASE("haptic table: the measured fw 5.29 table") {
    auto m = ixpat::measuredWaveforms();
    REQUIRE(m.size() == 8);
    CHECK(m[0].ordinal == 3);
    CHECK(m[0].usage == 0x1008);
    CHECK(m[0].durationMs == 9);
    CHECK(m[0].name() == "Hover");
    CHECK(m[7].ordinal == 10);
    CHECK(m[7].usage == 0x1003);
    CHECK(m[7].durationMs == 11);
    CHECK(m[7].name() == "Click");
    CHECK(m[4].name() == "Grow");
    CHECK(m[4].durationMs == 200);
}

TEST_CASE("haptic table: feature 0x42 parses back to the same table") {
    auto raw = featureFromTable(ixpat::measuredWaveforms());
    auto parsed = ixpat::parseHapticTable(raw);
    REQUIRE(parsed.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(parsed[i].ordinal == ixpat::measuredWaveforms()[i].ordinal);
        CHECK(parsed[i].usage == ixpat::measuredWaveforms()[i].usage);
        CHECK(parsed[i].durationMs == ixpat::measuredWaveforms()[i].durationMs);
    }
}

TEST_CASE("haptic table: short, empty or junk buffers yield an empty table") {
    CHECK(ixpat::parseHapticTable({}).empty());
    std::vector<std::uint8_t> shortBuf(200, 0);
    CHECK(ixpat::parseHapticTable(shortBuf).empty());
    std::vector<std::uint8_t> zeros(201, 0);
    CHECK(ixpat::parseHapticTable(zeros).empty());
    std::vector<std::uint8_t> junk(201, 0xFF);  // usage 0xFFFF is outside 0x1003..0x2FFF
    CHECK(ixpat::parseHapticTable(junk).empty());
}

TEST_CASE("haptic table: usages outside the haptic range are skipped, a longer report is fine") {
    auto raw = featureFromTable(ixpat::measuredWaveforms());
    raw[101] = 0x01;
    raw[102] = 0x10;  // ordinal 3 -> usage 0x1001 (None): skipped
    raw.resize(260, 0);
    auto parsed = ixpat::parseHapticTable(raw);
    REQUIRE(parsed.size() == 7);
    CHECK(parsed[0].ordinal == 4);
}

TEST_CASE("haptic table: names and lookups") {
    CHECK(ixpat::waveformName(0x1003) == "Click");
    CHECK(ixpat::waveformName(0x1015) == "Grow");
    CHECK(ixpat::waveformName(0x1234) == "Waveform 0x1234");
    std::string a = ixpat::waveformName(0x1111);
    for (int i = 0; i < 8; ++i) (void)ixpat::waveformName(static_cast<std::uint16_t>(0x2000 + i));
    CHECK(a == "Waveform 0x1111");  // an owning string, not a view into rotating storage
    CHECK(ixpat::ordinalFor(0x1012, ixpat::measuredWaveforms()) == 4);
    CHECK_FALSE(ixpat::ordinalFor(0x1004, ixpat::measuredWaveforms()).has_value());
    CHECK(ixpat::usageForLowercaseName("click") == 0x1003);
    CHECK(ixpat::usageForLowercaseName("align") == 0x1013);
    // The alias table is Bridge.swift's: snap/guide -> align, detent/tick -> step, boundary/limit/edge
    // -> collide, done/complete -> success, fail -> error, notify/progress -> grow, select/focus -> hover.
    CHECK(ixpat::usageForLowercaseName("snap") == 0x1013);
    CHECK(ixpat::usageForLowercaseName("guide") == 0x1013);
    CHECK(ixpat::usageForLowercaseName("detent") == 0x1014);
    CHECK(ixpat::usageForLowercaseName("tick") == 0x1014);
    CHECK(ixpat::usageForLowercaseName("boundary") == 0x1012);
    CHECK(ixpat::usageForLowercaseName("limit") == 0x1012);
    CHECK(ixpat::usageForLowercaseName("edge") == 0x1012);
    CHECK(ixpat::usageForLowercaseName("done") == 0x1009);
    CHECK(ixpat::usageForLowercaseName("complete") == 0x1009);
    CHECK(ixpat::usageForLowercaseName("fail") == 0x100A);
    CHECK(ixpat::usageForLowercaseName("notify") == 0x1015);
    CHECK(ixpat::usageForLowercaseName("progress") == 0x1015);
    CHECK(ixpat::usageForLowercaseName("select") == 0x1008);
    CHECK(ixpat::usageForLowercaseName("focus") == 0x1008);
    CHECK(ixpat::usageForLowercaseName("press") == 0x1003);
    CHECK_FALSE(ixpat::usageForLowercaseName("Click").has_value());  // callers lowercase first
    CHECK_FALSE(ixpat::usageForLowercaseName("teleport").has_value());
}
