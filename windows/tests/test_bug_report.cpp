#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "win/bug_report.h"
#include "win/paths.h"

using namespace ixpat::win;

TEST_CASE("bug report: the URL encoder keeps the unreserved set and escapes the rest") {
    CHECK(urlEncode("abc-XYZ_0.9~") == "abc-XYZ_0.9~");
    CHECK(urlEncode("a b&c=d\n") == "a%20b%26c%3Dd%0A");
    CHECK(urlEncode("caf\xC3\xA9") == "caf%C3%A9");
    CHECK(urlEncode("") == "");
}

TEST_CASE("bug report: the Windows build comes from the kernel") {
    const std::string b = windowsBuild();
    CHECK(b.rfind("10.0.", 0) == 0);
}

TEST_CASE("bug report: the issue URL carries the facts and names the bundle only when there is one") {
    ReportFacts f;
    f.version = "0.2.9 (x64)";
    f.device = "connected, firmware 5.29";
    const std::string plain = issueUrl(f, nullptr);
    CHECK(plain.rfind("https://github.com/knightynite/ixpat/issues/new?body=", 0) == 0);
    CHECK(plain.find("0.2.9%20%28x64%29") != std::string::npos);
    CHECK(plain.find("bundle") == std::string::npos);
    const std::filesystem::path z = "C:/somewhere/ixpat-report-20261025-101010.zip";
    f.includeDump = true;
    const std::string with = issueUrl(f, &z);
    CHECK(with.find("ixpat-report-20261025-101010.zip") != std::string::npos);
    CHECK(with.find("crash%20report") != std::string::npos);
    CHECK(with.find("somewhere") == std::string::npos);  // the folder stays private; only the file name goes
}

TEST_CASE("bug report: the bundle is a zip beside the logs with the dump when asked, and the dumps are found newest first") {
    // The test suite runs in a throwaway data directory (IXPAT_DATA_DIR): plant two dumps and a log there.
    const std::filesystem::path dir = appDataDir();
    for (const wchar_t* n : {L"crash-20260101-000000-1.dmp", L"crash-20260102-000000-2.dmp"}) {
        std::ofstream(dir / n, std::ios::binary) << "MDMP fake";
    }
    std::ofstream(dir / L"debug.log", std::ios::binary | std::ios::app) << "line\n";
    const auto dumps = crashDumps();
    REQUIRE(dumps.size() >= 2);
    CHECK(dumps.front().filename() == L"crash-20260102-000000-2.dmp");
    CHECK(newestCrashDumpTime() > 1700000000);  // after 2023: a real write time, not a zero
    ReportFacts f;
    f.version = "0.2.9 (x64)";
    f.device = "not connected";
    f.includeDump = true;
    std::string err;
    const auto zip = writeReportBundle(f, err);
    REQUIRE_MESSAGE(zip.has_value(), err);
    CHECK(zip->parent_path() == dir / L"reports");
    CHECK(zip->filename().wstring().rfind(L"ixpat-report-", 0) == 0);
    std::ifstream in(*zip, std::ios::binary);
    char magic[4]{};
    in.read(magic, 4);
    CHECK(std::string(magic, 4) == "PK\x03\x04");
    in.seekg(0, std::ios::end);
    CHECK(in.tellg() > 100);
    std::error_code ec;
    for (const auto& d : dumps) std::filesystem::remove(d, ec);
    std::filesystem::remove(*zip, ec);
}
