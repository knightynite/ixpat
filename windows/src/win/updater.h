#pragma once
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include "core/updater_parse.h"

namespace ixpat::win {

/// Outcome of one check, posted to main as (WPARAM manual, LPARAM UpdateResult*): the receiver owns the pointer.
struct UpdateResult {
    bool ok = false;
    std::string error;                   // when !ok: what to tell the user
    std::optional<ReleaseInfo> release;  // when ok: the newest Windows release, if any
};

/// Persisted in <data dir>\update-state.json (the Mac keeps these in UserDefaults).
struct UpdateState {
    std::int64_t lastCheck = 0;  // unix seconds of the last successful check
    std::string skipped;         // a version the user chose to skip
};
UpdateState loadUpdateState();
void saveUpdateState(const UpdateState& s);
std::int64_t unixNow();

/// The WinHTTP session handle of a request in flight, shared with whoever wants to abort it: closing the session
/// from another thread makes the synchronous calls fail at once.
struct SessionSlot {
    SRWLOCK lock = SRWLOCK_INIT;
    HINTERNET handle = nullptr;
    bool closed = false;
    void set(HINTERNET h);  // closes h at once if close() already ran
    void close();           // idempotent
};

/// GET https://<host><path> with WinHTTP (system proxy, 10 s timeouts). HTTP 200 fills body; anything else fills
/// error with a sentence for the user. `slot`, when given, owns the session handle so the call can be aborted.
bool httpsGet(const wchar_t* host, const wchar_t* path, const wchar_t* userAgent, std::string& body, std::string& error,
              SessionSlot* slot = nullptr);

/// The GitHub release check on a worker thread (Updater.swift): never downloads or runs anything; the release page
/// is opened in the browser because Windows builds are unsigned.
class Updater {
public:
    ~Updater();
    void init(HWND hwnd, UINT message, std::wstring userAgent);
    /// Starts a check unless one is running. Results arrive on hwnd as `message`.
    bool check(bool manual);
    bool busy() const { return busy_.load(); }
    void stop();

private:
    HWND hwnd_ = nullptr;
    UINT message_ = 0;
    std::wstring userAgent_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    SessionSlot session_;
};

}  // namespace ixpat::win
