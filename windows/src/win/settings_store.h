#pragma once
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <optional>
#include <thread>

#include "core/clock.h"
#include "core/settings.h"

namespace ixpat::win {

/// settings.json on disk: tolerant load, atomic save, debounced writes, and a watcher for edits made
/// outside the app (the file is user-editable, and there is no settings window until Plan 4).
class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file);
    ~SettingsStore();
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    /// Defaults when the file is missing. An unreadable file is kept as settings.broken.json for
    /// inspection and defaults are returned; the original is never overwritten by load().
    Settings load();
    /// Atomic: writes <file>.tmp, then MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH).
    void save(const Settings& s);

    /// Debounced saving on the caller's thread: markDirty() after each change, flushIfDue() from a
    /// 100 ms timer (writes once 300 ms after the last change), flush() on quit.
    void markDirty(const Settings& s, Seconds now);
    bool flushIfDue(Seconds now);
    void flush();

    /// Posts `message` to `hwnd` when the file changes on disk (debounced 300 ms); our own saves are
    /// ignored for 500 ms after they happen.
    void watch(HWND hwnd, UINT message);
    void stopWatching();

private:
    void watchLoop();

    std::filesystem::path file_;
    std::optional<Settings> pending_;
    Seconds dirtyAt_ = 0;

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT message_ = 0;
    std::atomic<unsigned long long> ignoreUntilTick_{0};  // GetTickCount64 deadline after our own write
};

}  // namespace ixpat::win
