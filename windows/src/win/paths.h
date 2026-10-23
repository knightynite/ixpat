#pragma once
#include <filesystem>

namespace ixpat::win {

/// %LOCALAPPDATA%\iXpat (created on first call). Settings and logs live here, like the Mac's
/// ~/Library/Application Support/iXpat.
std::filesystem::path appDataDir();
std::filesystem::path settingsPath();  // appDataDir()/settings.json

}  // namespace ixpat::win
