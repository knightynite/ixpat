#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace ixpat {

/// A Windows release on GitHub: the newest non-draft `win-v*` tag (Updater.swift reads `releases/latest`, which is
/// the Mac release; Windows builds are pre-releases under their own tag prefix so the Mac updater never sees them).
struct ReleaseInfo {
    std::string tag;           // "win-v0.2.1"
    std::string version;       // "0.2.1"
    std::string url;           // the release page, built from the tag
    bool installer = false;    // the release lists the x64 installer asset (installerAssetName)
    std::string installerUrl;  // its download URL, built from the tag and the version, never taken from the body
};

/// "win-v" followed by digits and dots only: the only tags the updater will ever open a page for.
bool isWindowsReleaseTag(std::string_view tag);
/// The release page for a valid tag, built here rather than taken from the response body.
std::string canonicalReleaseUrl(std::string_view tag);
/// "iXpat-Setup-<version>-x64.exe": the installer asset CI attaches to a Windows release.
std::string installerAssetName(std::string_view version);
/// The installer's download URL for a valid tag: GitHub's fixed layout, not the body's browser_download_url.
std::string canonicalInstallerUrl(std::string_view tag, std::string_view version);

/// Parses the body of GET /repos/<repo>/releases. Malformed JSON, a non-array body (an API error object) or no
/// Windows release gives nothing; an entry missing its tag or url is skipped.
std::optional<ReleaseInfo> newestWindowsRelease(std::string_view releasesJson);

}  // namespace ixpat
