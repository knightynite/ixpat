#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>

#include "win/paths.h"

TEST_CASE("paths: IXPAT_DATA_DIR overrides the data directory, so tests never touch the real one") {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"IXPAT_DATA_DIR", buf, MAX_PATH);
    REQUIRE(n > 0);
    CHECK(ixpat::win::appDataDir() == std::filesystem::path(buf));
    CHECK(std::filesystem::exists(ixpat::win::appDataDir()));
    CHECK(ixpat::win::settingsPath().parent_path() == std::filesystem::path(buf));
}
