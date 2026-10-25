#pragma once
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
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

/// Outcome of one download-and-verify, posted to main as (WPARAM 0, LPARAM InstallResult*): the receiver owns it.
struct InstallResult {
    bool ok = false;
    std::string error;           // when !ok: why nothing will be run
    std::string version;         // the release it was for
    std::filesystem::path file;  // when ok: the installer, downloaded and verified against the pinned publisher
};

/// Persisted in <data dir>\update-state.json (the Mac keeps these in UserDefaults).
struct UpdateState {
    std::int64_t lastCheck = 0;         // unix seconds of the last successful check
    std::string skipped;                // a version the user chose to skip
    std::int64_t lastCrashNoticed = 0;  // unix seconds: crash dumps written before this have been offered for a report
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

/// GET https://<host><path> streamed into `file` (an HTTPS redirect to another host, GitHub's download storage, is
/// followed; never to plain HTTP). More than `maxBytes` or anything but HTTP 200 fails and leaves no file behind.
bool httpsDownload(const wchar_t* host, const wchar_t* path, const wchar_t* userAgent, const std::filesystem::path& file,
                   std::size_t maxBytes, std::string& error, SessionSlot* slot = nullptr);

/// The GitHub release check and the update download, each on a worker thread (Updater.swift). A check only reads
/// the release list. An install downloads the release's installer from GitHub's fixed URL, verifies its Authenticode
/// signature against the pinned publisher (win/authenticode.h) and hands the file back; the app runs it as it quits.
/// Nothing unsigned, and nothing signed by anyone else, is ever run.
class Updater {
public:
    ~Updater();
    void init(HWND hwnd, UINT checkMessage, UINT installMessage, std::wstring userAgent);
    /// Starts a check unless a check or install is running. Results arrive on hwnd as checkMessage.
    bool check(bool manual);
    /// Downloads and verifies r's installer unless a check or install is running. Results arrive as installMessage.
    bool install(const ReleaseInfo& r);
    /// Removes the installers earlier updates left in <data dir>\updates. Nothing happens while a download runs.
    void discardDownloads();
    bool busy() const { return busy_.load(); }
    void stop();

private:
    bool begin();  // claims the worker: false when one is running
    HWND hwnd_ = nullptr;
    UINT checkMessage_ = 0, installMessage_ = 0;
    std::wstring userAgent_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    SessionSlot session_;
};

}  // namespace ixpat::win
