#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "core/zip_writer.h"

using ixpat::crc32;
using ixpat::ZipWriter;

namespace {
std::uint32_t at32(const std::vector<unsigned char>& v, std::size_t i) {
    return v[i] | (v[i + 1] << 8) | (v[i + 2] << 16) | (static_cast<std::uint32_t>(v[i + 3]) << 24);
}
std::uint16_t at16(const std::vector<unsigned char>& v, std::size_t i) { return static_cast<std::uint16_t>(v[i] | (v[i + 1] << 8)); }
}  // namespace

TEST_CASE("zip: crc32 is the IEEE one, and continues across calls") {
    CHECK(crc32("123456789", 9) == 0xCBF43926u);
    CHECK(crc32("", 0) == 0u);
    const std::uint32_t part = crc32("12345", 5);
    CHECK(crc32("6789", 4, part) == 0xCBF43926u);
}

TEST_CASE("zip: two stored entries, the directory points back at them, the end record counts them") {
    ZipWriter z;
    const std::string a = "hello\n", b = "";
    z.add("summary.txt", a.data(), a.size(), 0x5A21, 0x4800);  // 2025-01-01 09:00
    z.add("logs/empty.log", b.data(), b.size());
    const auto out = z.finish();
    REQUIRE(z.entries() == 2);
    // the first local header
    CHECK(at32(out, 0) == 0x04034B50u);
    CHECK(at16(out, 8) == 0);          // stored
    CHECK(at16(out, 10) == 0x4800);    // time
    CHECK(at16(out, 12) == 0x5A21);    // date
    CHECK(at32(out, 14) == crc32(a.data(), a.size()));
    CHECK(at32(out, 18) == a.size());
    CHECK(at16(out, 26) == 11);  // name length
    CHECK(std::memcmp(&out[30], "summary.txt", 11) == 0);
    CHECK(std::memcmp(&out[41], a.data(), a.size()) == 0);
    const std::size_t second = 30 + 11 + a.size();
    CHECK(at32(out, second) == 0x04034B50u);
    CHECK(at16(out, second + 12) == 0x0021);  // the default date: 1980-01-01
    // the end record is the last 22 bytes and points at a directory of two entries
    const std::size_t end = out.size() - 22;
    CHECK(at32(out, end) == 0x06054B50u);
    CHECK(at16(out, end + 8) == 2);
    CHECK(at16(out, end + 10) == 2);
    const std::uint32_t cdSize = at32(out, end + 12), cdStart = at32(out, end + 16);
    CHECK(cdStart + cdSize == end);
    CHECK(at32(out, cdStart) == 0x02014B50u);
    CHECK(at32(out, cdStart + 42) == 0);       // offset of the first entry
    CHECK(at32(out, cdStart + 46 + 11) == 0x02014B50u);
    CHECK(at32(out, cdStart + 46 + 11 + 42) == second);  // offset of the second
}

TEST_CASE("zip: an empty archive is just the end record") {
    const auto out = ZipWriter().finish();
    CHECK(out.size() == 22);
    CHECK(at32(out, 0) == 0x06054B50u);
}
