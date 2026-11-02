#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/updater_parse.h"
#include "win/paths.h"
#include "win/updater.h"

using ixpat::newestWindowsRelease;

namespace {
const char* kReleases = R"([
  {"tag_name":"v0.1.1","draft":false,"prerelease":false,"html_url":"https://github.com/knightynite/ixpat/releases/tag/v0.1.1"},
  {"tag_name":"win-v0.2.0","draft":false,"prerelease":true,"html_url":"https://github.com/knightynite/ixpat/releases/tag/win-v0.2.0"},
  {"tag_name":"win-v0.3.0","draft":true,"prerelease":true,"html_url":"https://github.com/knightynite/ixpat/releases/tag/win-v0.3.0"},
  {"tag_name":"win-v0.2.1","draft":false,"prerelease":true,"html_url":"https://github.com/knightynite/ixpat/releases/tag/win-v0.2.1"},
  {"tag_name":"win-v0.1.9","draft":false,"prerelease":true,"html_url":"https://github.com/knightynite/ixpat/releases/tag/win-v0.1.9"}
])";
}

TEST_CASE("updater: the newest non-draft win-v release wins; Mac tags and drafts are ignored") {
    auto r = newestWindowsRelease(kReleases, "x64");
    REQUIRE(r.has_value());
    CHECK(r->tag == "win-v0.2.1");
    CHECK(r->version == "0.2.1");
    CHECK(r->url == "https://github.com/knightynite/ixpat/releases/tag/win-v0.2.1");
}

TEST_CASE("updater: no Windows release, malformed JSON and non-array bodies give nothing") {
    CHECK_FALSE(newestWindowsRelease(R"([{"tag_name":"v0.1.1","draft":false,"html_url":"x"}])", "x64").has_value());
    CHECK_FALSE(newestWindowsRelease("{not json", "x64").has_value());
    CHECK_FALSE(newestWindowsRelease(R"({"message":"API rate limit exceeded"})", "x64").has_value());
    CHECK_FALSE(newestWindowsRelease("[]", "x64").has_value());
}

TEST_CASE("updater: a release missing its url or tag is skipped, not fatal") {
    auto r = newestWindowsRelease(R"([{"tag_name":"win-v9.0.0"},{"tag_name":"win-v0.2.5","draft":false,"html_url":"u"}])", "x64");
    REQUIRE(r.has_value());
    CHECK(r->version == "0.2.5");
}

TEST_CASE("updater: only a well-formed win-v tag is a Windows release, and its page URL is built from the tag, not the body") {
    using ixpat::canonicalReleaseUrl;
    using ixpat::isWindowsReleaseTag;
    CHECK(isWindowsReleaseTag("win-v0.2.0"));
    CHECK(isWindowsReleaseTag("win-v10.0.12"));
    CHECK_FALSE(isWindowsReleaseTag("v0.1.1"));
    CHECK_FALSE(isWindowsReleaseTag("win-v"));
    CHECK_FALSE(isWindowsReleaseTag("win-v0.2.0/../../evil"));
    CHECK_FALSE(isWindowsReleaseTag("win-v0.2.0 "));
    CHECK(canonicalReleaseUrl("win-v0.2.0") == "https://github.com/knightynite/ixpat/releases/tag/win-v0.2.0");
    // the parser never trusts html_url for the page it opens
    auto r = ixpat::newestWindowsRelease(R"([{"tag_name":"win-v0.2.0","draft":false,"html_url":"https://evil.example/x"}])", "x64");
    REQUIRE(r.has_value());
    CHECK(r->url == "https://github.com/knightynite/ixpat/releases/tag/win-v0.2.0");
    CHECK_FALSE(ixpat::newestWindowsRelease(R"([{"tag_name":"win-vX","draft":false,"html_url":"u"}])", "x64").has_value());
}

TEST_CASE("updater: the installer asset is recognised by its exact name and its URL is built, not copied") {
    using ixpat::canonicalInstallerUrl;
    using ixpat::installerAssetName;
    CHECK(installerAssetName("0.2.9", "x64") == "iXpat-Setup-0.2.9-x64.exe");
    CHECK(canonicalInstallerUrl("win-v0.2.9", "0.2.9", "x64") == "https://github.com/knightynite/ixpat/releases/download/win-v0.2.9/iXpat-Setup-0.2.9-x64.exe");
    auto r = newestWindowsRelease(R"([{"tag_name":"win-v0.2.9","draft":false,"html_url":"u","assets":[
        {"name":"iXpat-0.2.9-windows-x64.zip","browser_download_url":"https://evil.example/a"},
        {"name":"iXpat-Setup-0.2.9-x64.exe","browser_download_url":"https://evil.example/b"},
        {"name":"SHA256SUMS"}]}])", "x64");
    REQUIRE(r.has_value());
    CHECK(r->installer);
    CHECK(r->installerUrl == "https://github.com/knightynite/ixpat/releases/download/win-v0.2.9/iXpat-Setup-0.2.9-x64.exe");
    // zips only, a differently named exe, or no assets at all: nothing to self-install
    auto zipOnly = newestWindowsRelease(R"([{"tag_name":"win-v0.2.9","draft":false,"html_url":"u","assets":[{"name":"iXpat-0.2.9-windows-x64.zip"}]}])", "x64");
    REQUIRE(zipOnly.has_value());
    CHECK_FALSE(zipOnly->installer);
    CHECK(zipOnly->installerUrl.empty());
    auto wrongName = newestWindowsRelease(R"([{"tag_name":"win-v0.2.9","draft":false,"html_url":"u","assets":[{"name":"iXpat-Setup-0.2.8-x64.exe"}]}])", "x64");
    REQUIRE(wrongName.has_value());
    CHECK_FALSE(wrongName->installer);
    auto none = newestWindowsRelease(R"([{"tag_name":"win-v0.2.9","draft":false,"html_url":"u"}])", "x64");
    REQUIRE(none.has_value());
    CHECK_FALSE(none->installer);
    // malformed asset entries are skipped, not fatal
    auto odd = newestWindowsRelease(R"([{"tag_name":"win-v0.2.9","draft":false,"html_url":"u","assets":[1,{"name":5},{"name":"iXpat-Setup-0.2.9-x64.exe"}]}])", "x64");
    REQUIRE(odd.has_value());
    CHECK(odd->installer);
}

TEST_CASE("updater: each PC looks for the installer of its own architecture, never the other one") {
    const char* both = R"([{"tag_name":"win-v0.2.15","draft":false,"html_url":"u","assets":[
        {"name":"iXpat-Setup-0.2.15-x64.exe"},{"name":"iXpat-Setup-0.2.15-arm64.exe"}]}])";
    auto arm = newestWindowsRelease(both, "arm64");
    REQUIRE(arm.has_value());
    CHECK(arm->arch == "arm64");
    CHECK(arm->installer);
    CHECK(arm->installerUrl == "https://github.com/knightynite/ixpat/releases/download/win-v0.2.15/iXpat-Setup-0.2.15-arm64.exe");
    auto x64 = newestWindowsRelease(both, "x64");
    REQUIRE(x64.has_value());
    CHECK(x64->installerUrl == "https://github.com/knightynite/ixpat/releases/download/win-v0.2.15/iXpat-Setup-0.2.15-x64.exe");
    // a release with the x64 installer only offers an ARM64 PC the release page, not the emulated build
    auto x64Only = newestWindowsRelease(R"([{"tag_name":"win-v0.2.14","draft":false,"html_url":"u","assets":[{"name":"iXpat-Setup-0.2.14-x64.exe"}]}])", "arm64");
    REQUIRE(x64Only.has_value());
    CHECK_FALSE(x64Only->installer);
    CHECK(x64Only->installerUrl.empty());
    // an arch that is not one of the two finds no installer, whatever the release lists
    CHECK(ixpat::isInstallerArch("x64"));
    CHECK(ixpat::isInstallerArch("arm64"));
    CHECK_FALSE(ixpat::isInstallerArch("x86"));
    CHECK_FALSE(ixpat::isInstallerArch("arm64/../x"));
    auto odd = newestWindowsRelease(R"([{"tag_name":"win-v0.2.15","draft":false,"html_url":"u","assets":[{"name":"iXpat-Setup-0.2.15-x86.exe"}]}])", "x86");
    REQUIRE(odd.has_value());
    CHECK_FALSE(odd->installer);
}

TEST_CASE("updater: this PC's installer arch is the machine's, not the build's") {
    const std::string arch = ixpat::win::installerArch();
    CHECK(ixpat::isInstallerArch(arch));
    USHORT process = 0, native = 0;
    REQUIRE(IsWow64Process2(GetCurrentProcess(), &process, &native));
    CHECK(arch == (native == IMAGE_FILE_MACHINE_ARM64 ? "arm64" : "x64"));
}

TEST_CASE("updater: discarding downloads empties the updates folder and touches nothing beside it") {
    const std::filesystem::path data = ixpat::win::appDataDir();
    const std::filesystem::path dir = data / L"updates";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / L"iXpat-Setup-0.2.8-x64.exe", std::ios::binary) << "MZ earlier";
    std::ofstream(data / L"keep.txt", std::ios::binary) << "x";
    ixpat::win::Updater u;
    u.discardDownloads();
    CHECK(std::filesystem::exists(dir));
    CHECK(std::filesystem::is_empty(dir));
    CHECK(std::filesystem::exists(data / L"keep.txt"));
    std::error_code ec;
    std::filesystem::remove(data / L"keep.txt", ec);
}
