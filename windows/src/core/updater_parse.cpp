#include "core/updater_parse.h"

#include <nlohmann/json.hpp>

#include "core/version.h"

namespace ixpat {

bool isWindowsReleaseTag(std::string_view tag) {
    constexpr std::string_view prefix = "win-v";
    if (tag.size() <= prefix.size() || tag.substr(0, prefix.size()) != prefix) return false;
    const std::string_view rest = tag.substr(prefix.size());
    bool digit = false;
    for (const char c : rest) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c != '.') return false;
    }
    return digit && rest.front() != '.' && rest.back() != '.';
}

std::string canonicalReleaseUrl(std::string_view tag) { return "https://github.com/knightynite/ixpat/releases/tag/" + std::string(tag); }

std::optional<ReleaseInfo> newestWindowsRelease(std::string_view releasesJson) {
    const nlohmann::json j = nlohmann::json::parse(releasesJson, nullptr, false);
    if (!j.is_array()) return std::nullopt;
    std::optional<ReleaseInfo> best;
    for (const auto& r : j) {
        if (!r.is_object()) continue;
        const auto tag = r.find("tag_name");
        const auto url = r.find("html_url");
        if (tag == r.end() || !tag->is_string() || url == r.end() || !url->is_string()) continue;
        if (const auto d = r.find("draft"); d != r.end() && d->is_boolean() && d->get<bool>()) continue;
        const std::string t = tag->get<std::string>();
        if (!isWindowsReleaseTag(t)) continue;
        ReleaseInfo info{t, stripTagPrefix(t), canonicalReleaseUrl(t)};  // html_url is read for shape only, never opened
        if (!best || isNewerVersion(info.version, best->version)) best = std::move(info);
    }
    return best;
}

}  // namespace ixpat
