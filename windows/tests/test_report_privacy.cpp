#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "win/bug_report.h"
#include "win/log.h"
#include "win/paths.h"

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.write(text.data(), static_cast<std::streamsize>(text.size())).good());
}

// Read the stored ZIP entries, checking bounds instead of searching compressed/container bytes.
std::map<std::string, std::string> readBundle(const std::filesystem::path& path) {
    const std::string bytes = readFile(path);
    auto integer = [&](std::size_t at, std::size_t count) {
        REQUIRE(at + count <= bytes.size());
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < count; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
        return value;
    };
    std::map<std::string, std::string> entries;
    std::size_t at = 0;
    while (integer(at, 4) == 0x04034b50u) {
        REQUIRE(integer(at + 8, 2) == 0);  // stored, not compressed
        const auto size = integer(at + 18, 4);
        const auto nameSize = integer(at + 26, 2);
        const auto extraSize = integer(at + 28, 2);
        const std::size_t data = at + 30 + nameSize + extraSize;
        REQUIRE(data + size <= bytes.size());
        entries.emplace(bytes.substr(at + 30, nameSize), bytes.substr(data, size));
        at = data + size;
    }
    REQUIRE(integer(at, 4) == 0x02014b50u);  // central directory
    return entries;
}

std::vector<std::string> keyEvents() {
    std::vector<std::string> lines;
    for (const char* vk : {"53", "86", "10", "a0", "a1", "5b", "5c"}) {
        for (const char* direction : {"down", "up"}) {
            const std::string event = std::string(vk) + " " + direction;
            lines.push_back("keyboard vk " + event + " (other device)");
            lines.push_back("keyboard vk " + event + " (no hook callback)");
            lines.push_back("hook vk " + event + " passed");
            lines.push_back("hook vk " + event + " swallowed");
        }
    }
    return lines;
}

}  // namespace

TEST_CASE("report privacy: live logs discard key traces but retain mouse and health events") {
    const auto path = ixpat::win::appDataDir() / L"live-privacy.log";
    ixpat::win::FileLog log(path);
    for (const auto& event : keyEvents()) log.write(event);
    // A multiline caller must not smuggle a key trace after an ordinary line.
    log.write("hooks installed\nhook vk 53 down passed");
    log.write("mouse buttons 00 -> 01");
    log.write("action button: Press");
    log.write("watchdog: raw input flowing but no keyboard hook callbacks; re-installing hooks");
    const std::string text = readFile(path);
    CHECK(text.find("keyboard vk") == std::string::npos);
    CHECK(text.find("hook vk") == std::string::npos);
    CHECK(text.find("mouse buttons 00 -> 01") != std::string::npos);
    CHECK(text.find("action button: Press") != std::string::npos);
    CHECK(text.find("no keyboard hook callbacks") != std::string::npos);
}

TEST_CASE("report privacy: exported ZIP removes legacy key traces from both logs") {
    const auto dir = ixpat::win::appDataDir();
    std::string legacy = "2026-10-26T00:00:00.000Z mouse buttons 00 -> 01\r\n";
    for (const auto& event : keyEvents()) legacy += "2026-10-26T00:00:00.001Z " + event + "\r\n";
    legacy += "2026-10-26T00:00:00.002Z action button: Press\n";
    legacy += "2026-10-26T00:00:00.003Z hook vk 53 up passed";  // no final newline
    writeFile(dir / L"reports.log", legacy);
    writeFile(dir / L"debug.log", legacy);
    writeFile(ixpat::win::settingsPath(), "{\"bridgeToken\":\"private-test-token\",\"masterIntensity\":2}");
    const auto dump = dir / L"crash-9999-privacy.dmp";
    writeFile(dump, "optional-dump-marker");

    std::string error;
    ixpat::win::ReportFacts facts{"test", "not connected", false};
    const auto path = ixpat::win::writeReportBundle(facts, error);
    REQUIRE(path.has_value());
    CHECK(error.empty());
    const auto entries = readBundle(*path);
    for (const char* name : {"debug.log", "reports.log"}) {
        REQUIRE(entries.contains(name));
        CHECK(entries.at(name) == "2026-10-26T00:00:00.000Z mouse buttons 00 -> 01\r\n2026-10-26T00:00:00.002Z action button: Press\n");
    }
    CHECK(entries.at("settings.json").find("private-test-token") == std::string::npos);
    CHECK(entries.at("settings.json").find("masterIntensity") != std::string::npos);
    CHECK_FALSE(entries.contains("crash-9999-privacy.dmp"));
    CHECK(readFile(dir / L"reports.log") == legacy);  // export never edits the user's old files

    facts.includeDump = true;
    const auto withDump = ixpat::win::writeReportBundle(facts, error);
    REQUIRE(withDump.has_value());
    CHECK(readBundle(*withDump).at("crash-9999-privacy.dmp") == "optional-dump-marker");
    std::filesystem::remove(dump);
}

TEST_CASE("report privacy: tail limits never export a partial legacy record") {
    const auto dir = ixpat::win::appDataDir();
    // The tail begins after the key-event prefix; there is no complete line to export.
    writeFile(dir / L"reports.log", "2026-10-26T00:00:00.000Z keyboard vk 53 down " + std::string(256u << 10, 'x'));
    writeFile(dir / L"debug.log", std::string(256u << 10, 'x') + "\nkeyboard vk 53 down (other device)\n" +
                                   "hooks installed\nhook vk 53 up passed\nmouse buttons 01 -> 00\n");
    std::string error;
    const auto path = ixpat::win::writeReportBundle({"test", "not connected", false}, error);
    REQUIRE(path.has_value());
    const auto entries = readBundle(*path);
    CHECK(entries.at("reports.log").empty());
    CHECK(entries.at("debug.log") == "hooks installed\nmouse buttons 01 -> 00\n");
}
