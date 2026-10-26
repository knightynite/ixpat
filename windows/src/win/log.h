#pragma once
#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace ixpat::win {

/// Append-only text log next to the settings file, capped in size: when it passes `kLimit` it is
/// rotated to `<name>.1` and started afresh (the Mac's FileLog). Thread-safe; never called from a hook.
class FileLog {
public:
    static constexpr std::uintmax_t kLimit = 512 * 1024;
    explicit FileLog(std::filesystem::path file);
    /// Writes "<ISO-8601 UTC> <line>\n", discarding legacy keyboard-event diagnostic messages.
    void write(std::string_view line);

private:
    std::filesystem::path file_;
    SRWLOCK lock_ = SRWLOCK_INIT;
};

/// debug.log: device attach/detach, permission and hook state, gestures, errors. No key, menu or text content.
void debugLog(std::string_view s);
/// reports.log: the mouse's button transitions, never keyboard events or pointer motion.
void reportLog(std::string_view s);
/// Drops whole keyboard/hook key-event lines, including timestamped traces retained by older versions.
/// Used at export as well as at the live file boundary; does not modify existing files.
std::string withoutKeyEvents(std::string_view text);
/// debug.log: "slow: <what> took <ms> ms", at most four lines a second per calling thread. Never from a hook
/// callback: the input thread posts its slow paths to main instead.
void debugLogSlow(const char* what, double ms);
/// Milliseconds since `qpc`, a QueryPerformanceCounter value.
double msSince(long long qpc);
long long qpcTicks();

}  // namespace ixpat::win
