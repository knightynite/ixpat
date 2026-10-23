#include "win/settings_store.h"

#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "win/log.h"

namespace ixpat::win {
namespace {

constexpr Seconds kDebounce = 0.3;
constexpr unsigned long long kIgnoreOwnWriteMs = 500;
constexpr DWORD kWatchDebounceMs = 300;

bool writeWholeFile(const std::filesystem::path& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && written == data.size();
}

}  // namespace

SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) {}

SettingsStore::~SettingsStore() { stopWatching(); }

Settings SettingsStore::load() {
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) return defaultSettings();
    std::ifstream in(file_, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    auto parsed = parseSettings(text);
    if (auto* err = std::get_if<ParseError>(&parsed)) {
        // Keep the unreadable file for inspection instead of silently overwriting it.
        std::filesystem::path broken = file_;
        broken.replace_extension(L".broken.json");
        std::filesystem::copy_file(file_, broken, std::filesystem::copy_options::overwrite_existing, ec);
        debugLog("settings.json could not be decoded (" + err->message + "); kept as settings.broken.json");
        return defaultSettings();
    }
    return std::get<Settings>(std::move(parsed));
}

void SettingsStore::save(const Settings& s) {
    std::filesystem::path tmp = file_;
    tmp += L".tmp";
    ignoreUntilTick_ = GetTickCount64() + kIgnoreOwnWriteMs;
    if (!writeWholeFile(tmp, toJson(s))) {
        debugLog("settings: could not write " + tmp.string());
        return;
    }
    if (!MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        debugLog("settings: MoveFileEx failed, error " + std::to_string(GetLastError()));
        DeleteFileW(tmp.c_str());
    }
    ignoreUntilTick_ = GetTickCount64() + kIgnoreOwnWriteMs;
}

void SettingsStore::markDirty(const Settings& s, Seconds now) {
    pending_ = s;
    dirtyAt_ = now;
}

bool SettingsStore::flushIfDue(Seconds now) {
    if (!pending_ || now - dirtyAt_ < kDebounce) return false;
    save(*pending_);
    pending_.reset();
    return true;
}

void SettingsStore::flush() {
    if (!pending_) return;
    save(*pending_);
    pending_.reset();
}

void SettingsStore::watch(HWND hwnd, UINT message) {
    if (thread_.joinable()) return;
    hwnd_ = hwnd;
    message_ = message;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { watchLoop(); });
}

void SettingsStore::stopWatching() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void SettingsStore::watchLoop() {
    const std::filesystem::path dir = file_.parent_path();
    const std::wstring name = file_.filename().wstring();
    HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        debugLog("settings watch: cannot open directory, error " + std::to_string(GetLastError()));
        return;
    }
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::vector<std::uint8_t> buf(16 * 1024);
    OVERLAPPED ov{};
    ov.hEvent = changed;
    constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    bool armed = ReadDirectoryChangesW(h, buf.data(), static_cast<DWORD>(buf.size()), FALSE, filter, nullptr, &ov, nullptr) != 0;
    unsigned long long notifyAt = 0;  // tick when the debounced message is due; 0 = nothing pending
    HANDLE waits[2] = {stopEvent_, changed};
    while (armed) {
        const unsigned long long now = GetTickCount64();
        DWORD timeout = INFINITE;
        if (notifyAt) timeout = notifyAt > now ? static_cast<DWORD>(notifyAt - now) : 0;
        const DWORD r = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (r == WAIT_OBJECT_0) break;  // stop
        if (r == WAIT_OBJECT_0 + 1) {
            DWORD bytes = 0;
            if (GetOverlappedResult(h, &ov, &bytes, FALSE) && bytes) {
                std::size_t off = 0;
                bool relevant = false;
                while (off < bytes) {
                    const auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf.data() + off);
                    std::wstring changedName(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
                    if (_wcsicmp(changedName.c_str(), name.c_str()) == 0) relevant = true;
                    if (!fni->NextEntryOffset) break;
                    off += fni->NextEntryOffset;
                }
                if (relevant && GetTickCount64() > ignoreUntilTick_.load()) notifyAt = GetTickCount64() + kWatchDebounceMs;
            }
            armed = ReadDirectoryChangesW(h, buf.data(), static_cast<DWORD>(buf.size()), FALSE, filter, nullptr, &ov, nullptr) != 0;
        } else if (r == WAIT_TIMEOUT && notifyAt) {
            notifyAt = 0;
            PostMessageW(hwnd_, message_, 0, 0);
        } else if (r == WAIT_FAILED) {
            break;
        }
    }
    CancelIoEx(h, &ov);
    CloseHandle(changed);
    CloseHandle(h);
}

}  // namespace ixpat::win
