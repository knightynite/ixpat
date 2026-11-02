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
    std::string arch;          // the installer looked for: "x64" or "arm64"
    bool installer = false;    // the release lists that installer asset (installerAssetName)
    std::string installerUrl;  // its download URL, built from the tag, version and arch, never taken from the body
};

/// "win-v" followed by digits and dots only: the only tags the updater will ever open a page for.
bool isWindowsReleaseTag(std::string_view tag);
/// The release page for a valid tag, built here rather than taken from the response body.
std::string canonicalReleaseUrl(std::string_view tag);
/// "x64" or "arm64": the architectures CI builds an installer for.
bool isInstallerArch(std::string_view arch);
/// "iXpat-Setup-<version>-<arch>.exe": the installer asset CI attaches to a Windows release.
std::string installerAssetName(std::string_view version, std::string_view arch);
/// The installer's download URL for a valid tag: GitHub's fixed layout, not the body's browser_download_url.
std::string canonicalInstallerUrl(std::string_view tag, std::string_view version, std::string_view arch);

/// Parses the body of GET /repos/<repo>/releases. Malformed JSON, a non-array body (an API error object) or no
/// Windows release gives nothing; an entry missing its tag or url is skipped. `arch` names the installer looked
/// for; anything but an installer arch finds none.
std::optional<ReleaseInfo> newestWindowsRelease(std::string_view releasesJson, std::string_view arch);

}  // namespace ixpat
