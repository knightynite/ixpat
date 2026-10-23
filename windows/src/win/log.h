#pragma once
#include <windows.h>

#include <filesystem>
#include <string_view>

namespace ixpat::win {

/// Append-only text log next to the settings file, capped in size: when it passes `kLimit` it is
/// rotated to `<name>.1` and started afresh (the Mac's FileLog). Thread-safe; never called from a hook.
class FileLog {
public:
    static constexpr std::uintmax_t kLimit = 512 * 1024;
    explicit FileLog(std::filesystem::path file);
    /// Writes "<ISO-8601 UTC> <line>\n".
    void write(std::string_view line);

private:
    std::filesystem::path file_;
    SRWLOCK lock_ = SRWLOCK_INIT;
};

/// debug.log: device attach/detach, permission and hook state, gestures, errors. No key, menu or text content.
void debugLog(std::string_view s);
/// reports.log: summaries of the mouse's own button, key and vendor reports, never pointer motion.
void reportLog(std::string_view s);

}  // namespace ixpat::win
