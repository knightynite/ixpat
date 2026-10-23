#pragma once
#include <string>
#include <string_view>

namespace ixpat {

/// Dotted numeric compare, as Updater.isNewer: missing parts are 0, non-numeric parts are 0.
bool isNewerVersion(std::string_view candidate, std::string_view current);

/// "win-v0.2.0" -> "0.2.0", "v0.1.1" -> "0.1.1".
std::string stripTagPrefix(std::string_view tag);

}  // namespace ixpat
