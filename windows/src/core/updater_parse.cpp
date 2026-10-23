#include "core/updater_parse.h"

#include <nlohmann/json.hpp>

#include "core/version.h"

namespace ixpat {

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
        if (t.rfind("win-v", 0) != 0) continue;
        ReleaseInfo info{t, stripTagPrefix(t), url->get<std::string>()};
        if (!best || isNewerVersion(info.version, best->version)) best = std::move(info);
    }
    return best;
}

}  // namespace ixpat
