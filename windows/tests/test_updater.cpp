#include <doctest/doctest.h>

#include <string>

#include "core/updater_parse.h"

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
    auto r = newestWindowsRelease(kReleases);
    REQUIRE(r.has_value());
    CHECK(r->tag == "win-v0.2.1");
    CHECK(r->version == "0.2.1");
    CHECK(r->url == "https://github.com/knightynite/ixpat/releases/tag/win-v0.2.1");
}

TEST_CASE("updater: no Windows release, malformed JSON and non-array bodies give nothing") {
    CHECK_FALSE(newestWindowsRelease(R"([{"tag_name":"v0.1.1","draft":false,"html_url":"x"}])").has_value());
    CHECK_FALSE(newestWindowsRelease("{not json").has_value());
    CHECK_FALSE(newestWindowsRelease(R"({"message":"API rate limit exceeded"})").has_value());
    CHECK_FALSE(newestWindowsRelease("[]").has_value());
}

TEST_CASE("updater: a release missing its url or tag is skipped, not fatal") {
    auto r = newestWindowsRelease(R"([{"tag_name":"win-v9.0.0"},{"tag_name":"win-v0.2.5","draft":false,"html_url":"u"}])");
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
    auto r = ixpat::newestWindowsRelease(R"([{"tag_name":"win-v0.2.0","draft":false,"html_url":"https://evil.example/x"}])");
    REQUIRE(r.has_value());
    CHECK(r->url == "https://github.com/knightynite/ixpat/releases/tag/win-v0.2.0");
    CHECK_FALSE(ixpat::newestWindowsRelease(R"([{"tag_name":"win-vX","draft":false,"html_url":"u"}])").has_value());
}
