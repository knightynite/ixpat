#include <doctest/doctest.h>
#include <windows.h>

#include <string>

#include "win/login_item.h"

using namespace ixpat::win;

TEST_CASE("login item: the Run command is this executable's quoted path") {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    CHECK(loginItemCommand() == L"\"" + std::wstring(exe) + L"\"");
}

TEST_CASE("login item: enable / disable round-trip under a test value name") {
    const wchar_t* name = L"iXpat-test-login-item";
    setLoginItem(false, name);
    CHECK_FALSE(loginItemEnabled(name));
    REQUIRE(setLoginItem(true, name));
    CHECK(loginItemEnabled(name));
    REQUIRE(setLoginItem(false, name));
    CHECK_FALSE(loginItemEnabled(name));
}
