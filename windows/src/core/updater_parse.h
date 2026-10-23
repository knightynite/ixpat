#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace ixpat {

/// A Windows release on GitHub: the newest non-draft `win-v*` tag (Updater.swift reads `releases/latest`, which is
/// the Mac release; Windows builds are pre-releases under their own tag prefix so the Mac updater never sees them).
struct ReleaseInfo {
    std::string tag;      // "win-v0.2.1"
    std::string version;  // "0.2.1"
    std::string url;      // html_url of the release page
};

/// "win-v" followed by digits and dots only: the only tags the updater will ever open a page for.
bool isWindowsReleaseTag(std::string_view tag);
/// The release page for a valid tag, built here rather than taken from the response body.
std::string canonicalReleaseUrl(std::string_view tag);

/// Parses the body of GET /repos/<repo>/releases. Malformed JSON, a non-array body (an API error object) or no
/// Windows release gives nothing; an entry missing its tag or url is skipped.
std::optional<ReleaseInfo> newestWindowsRelease(std::string_view releasesJson);

}  // namespace ixpat
