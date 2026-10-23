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
