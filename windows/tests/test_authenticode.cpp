#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <string>

#include "win/authenticode.h"

using ixpat::win::checkAuthenticode;
using ixpat::win::isPinnedPublisher;

TEST_CASE("authenticode: an unsigned file (this test binary) is not trusted and names the reason") {
    wchar_t exe[MAX_PATH]{};
    REQUIRE(GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0);
    const auto c = checkAuthenticode(exe);
    CHECK_FALSE(c.trusted);
    CHECK(c.error == "The file is not signed.");
    CHECK(c.commonName.empty());
    CHECK_FALSE(isPinnedPublisher(c));
}

TEST_CASE("authenticode: a missing file is not trusted and does not crash") {
    const auto c = checkAuthenticode(std::filesystem::temp_directory_path() / L"ixpat-no-such-file.exe");
    CHECK_FALSE(c.trusted);
    CHECK_FALSE(c.error.empty());
    CHECK_FALSE(isPinnedPublisher(c));
}

TEST_CASE("authenticode: a file signed by someone else is trusted by Windows but is not the pinned publisher") {
    // Edge carries an embedded signature (Windows' own binaries are catalog-signed, which this check ignores on purpose).
    const wchar_t* candidates[] = {L"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
                                   L"C:/Program Files/Microsoft/Edge/Application/msedge.exe"};
    const wchar_t* found = nullptr;
    for (const wchar_t* p : candidates) {
        if (std::filesystem::exists(p)) {
            found = p;
            break;
        }
    }
    if (!found) {
        MESSAGE("no embedded-signed reference file on this machine; skipped");
        return;
    }
    const auto c = checkAuthenticode(found);
    CHECK(c.trusted);
    CHECK(c.commonName == L"Microsoft Corporation");
    CHECK(c.organization == L"Microsoft Corporation");
    CHECK_FALSE(isPinnedPublisher(c));  // trusted is not enough: the name must match
}

TEST_CASE("authenticode: the pin needs trust and both names") {
    ixpat::win::SignatureCheck c;
    c.commonName = ixpat::win::kUpdatePublisher;
    c.organization = ixpat::win::kUpdatePublisher;
    CHECK_FALSE(isPinnedPublisher(c));  // not trusted
    c.trusted = true;
    CHECK(isPinnedPublisher(c));
    c.organization = L"Someone Else";
    CHECK_FALSE(isPinnedPublisher(c));
    c.organization = ixpat::win::kUpdatePublisher;
    c.commonName = L"AL Najafi ";  // no normalisation: the exact subject or nothing
    CHECK_FALSE(isPinnedPublisher(c));
}
