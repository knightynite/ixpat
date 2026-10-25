#include "win/updater.h"

#include <winhttp.h>

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

#include "win/authenticode.h"
#include "win/log.h"
#include "win/paths.h"

namespace ixpat::win {
namespace {

constexpr std::size_t kMaxBody = 4u << 20;
constexpr std::size_t kMaxInstaller = 64u << 20;

std::string lastErrorText(DWORD code) {
    wchar_t* buf = nullptr;
    const HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (winhttp && code >= WINHTTP_ERROR_BASE && code <= WINHTTP_ERROR_LAST) flags |= FORMAT_MESSAGE_FROM_HMODULE;
    const DWORD n = FormatMessageW(flags, winhttp, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string s;
    if (n && buf) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        s.resize(static_cast<std::size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), s.data(), len, nullptr, nullptr);
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    }
    if (buf) LocalFree(buf);
    if (s.empty()) s = "Windows error " + std::to_string(code);
    return s;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::filesystem::path statePath() { return appDataDir() / L"update-state.json"; }

/// One GET: the handles, the status code, and the body read in chunks into whatever sink wants them.
struct Request {
    HINTERNET s = nullptr, c = nullptr, r = nullptr;
    SessionSlot* slot = nullptr;
    DWORD status = 0;
    ~Request() {
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        if (slot) slot->close();  // the slot owns the session once published
        else if (s) WinHttpCloseHandle(s);
    }
    bool open(const wchar_t* host, const wchar_t* path, const wchar_t* userAgent, const wchar_t* headers, SessionSlot* sl, std::string& error) {
        s = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) s = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) {
            error = lastErrorText(GetLastError());
            return false;
        }
        if (sl) {
            slot = sl;
            sl->set(s);
        }
        WinHttpSetTimeouts(s, 10000, 10000, 10000, 15000);
        c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!c) {
            error = lastErrorText(GetLastError());
            return false;
        }
        r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!r) {
            error = lastErrorText(GetLastError());
            return false;
        }
        if (headers) WinHttpAddRequestHeaders(r, headers, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
        if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, nullptr)) {
            error = lastErrorText(GetLastError());
            return false;
        }
        DWORD size = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        return true;
    }
    /// Reads the body chunk by chunk into sink(data, n); false with error on a transport failure or past maxBytes.
    template <class Sink>
    bool read(std::size_t maxBytes, Sink sink, std::string& error) {
        std::size_t total = 0;
        std::string chunk;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(r, &avail)) {
                error = lastErrorText(GetLastError());
                return false;
            }
            if (avail == 0) return true;
            if (total + avail > maxBytes) {
                error = "The response was too large.";
                return false;
            }
            chunk.resize(avail);
            DWORD got = 0;
            if (!WinHttpReadData(r, chunk.data(), avail, &got)) {
                error = lastErrorText(GetLastError());
                return false;
            }
            if (got == 0) return true;
            total += got;
            if (!sink(chunk.data(), static_cast<std::size_t>(got))) {
                error = "The file could not be written.";
                return false;
            }
        }
    }
};

}  // namespace

std::int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

UpdateState loadUpdateState() {
    UpdateState s;
    std::ifstream in(statePath(), std::ios::binary);
    if (!in) return s;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return s;
    if (auto it = j.find("lastCheck"); it != j.end() && it->is_number_integer()) s.lastCheck = it->get<std::int64_t>();
    if (auto it = j.find("skipped"); it != j.end() && it->is_string()) s.skipped = it->get<std::string>();
    if (auto it = j.find("lastCrashNoticed"); it != j.end() && it->is_number_integer()) s.lastCrashNoticed = it->get<std::int64_t>();
    return s;
}

void saveUpdateState(const UpdateState& s) {
    const nlohmann::json j{{"lastCheck", s.lastCheck}, {"skipped", s.skipped}, {"lastCrashNoticed", s.lastCrashNoticed}};
    const std::filesystem::path target = statePath();
    const std::filesystem::path tmp = target.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2) << '\n';
        if (!out) {
            debugLog("update state: could not be written");
            return;
        }
    }
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) debugLog("update state: could not be replaced");
}

void SessionSlot::set(HINTERNET h) {
    AcquireSRWLockExclusive(&lock);
    const bool late = closed;
    if (!late) handle = h;
    ReleaseSRWLockExclusive(&lock);
    if (late && h) WinHttpCloseHandle(h);
}

void SessionSlot::close() {
    AcquireSRWLockExclusive(&lock);
    HINTERNET h = handle;
    handle = nullptr;
    closed = true;
    ReleaseSRWLockExclusive(&lock);
    if (h) WinHttpCloseHandle(h);
}

bool httpsGet(const wchar_t* host, const wchar_t* path, const wchar_t* userAgent, std::string& body, std::string& error,
              SessionSlot* slot) {
    body.clear();
    error.clear();
    Request q;
    if (!q.open(host, path, userAgent, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28", slot, error)) return false;
    if (!q.read(kMaxBody, [&](const char* p, std::size_t n) { body.append(p, n); return true; }, error)) return false;
    if (q.status != 200) {
        error = q.status == 404 ? "No release has been published yet." : "GitHub answered HTTP " + std::to_string(q.status) + ".";
        return false;
    }
    return true;
}

bool httpsDownload(const wchar_t* host, const wchar_t* path, const wchar_t* userAgent, const std::filesystem::path& file,
                   std::size_t maxBytes, std::string& error, SessionSlot* slot) {
    error.clear();
    std::error_code ec;
    std::filesystem::remove(file, ec);
    bool ok = false;
    {
        Request q;
        if (!q.open(host, path, userAgent, nullptr, slot, error)) return false;
        if (q.status != 200) {
            error = "GitHub answered HTTP " + std::to_string(q.status) + " for the download.";
            return false;
        }
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "The download folder could not be written.";
            return false;
        }
        ok = q.read(maxBytes, [&](const char* p, std::size_t n) { return static_cast<bool>(out.write(p, static_cast<std::streamsize>(n))); }, error);
        out.close();
        if (ok && !out) {
            ok = false;
            error = "The download could not be written.";
        }
    }
    if (!ok) std::filesystem::remove(file, ec);
    return ok;
}

Updater::~Updater() { stop(); }

void Updater::init(HWND hwnd, UINT checkMessage, UINT installMessage, std::wstring userAgent) {
    hwnd_ = hwnd;
    checkMessage_ = checkMessage;
    installMessage_ = installMessage;
    userAgent_ = std::move(userAgent);
}

bool Updater::begin() {
    if (busy_.load() || !hwnd_) return false;
    if (thread_.joinable()) thread_.join();  // the previous job finished; reclaim it
    busy_ = true;
    AcquireSRWLockExclusive(&session_.lock);
    session_.handle = nullptr;
    session_.closed = false;
    ReleaseSRWLockExclusive(&session_.lock);
    return true;
}

bool Updater::check(bool manual) {
    if (!begin()) return false;
    thread_ = std::thread([this, manual] {
        auto* res = new UpdateResult;
        std::string body;
        if (httpsGet(L"api.github.com", L"/repos/knightynite/ixpat/releases?per_page=100", userAgent_.c_str(), body, res->error, &session_)) {
            if (nlohmann::json::parse(body, nullptr, false).is_array()) {
                res->ok = true;
                res->release = newestWindowsRelease(body);
            } else {
                res->error = "GitHub answered with something that is not a release list.";
            }
        }
        busy_ = false;
        if (!PostMessageW(hwnd_, checkMessage_, manual ? 1 : 0, reinterpret_cast<LPARAM>(res))) delete res;
    });
    return true;
}

bool Updater::install(const ReleaseInfo& r) {
    if (!r.installer || !isWindowsReleaseTag(r.tag)) return false;
    if (!begin()) return false;
    thread_ = std::thread([this, r] {
        auto* res = new InstallResult;
        res->version = r.version;
        // The tag passed isWindowsReleaseTag (digits and dots after the prefix), so the widening is exact.
        const std::string name = installerAssetName(r.version);
        const std::wstring wname(name.begin(), name.end());
        const std::wstring path = L"/knightynite/ixpat/releases/download/" + std::wstring(r.tag.begin(), r.tag.end()) + L"/" + wname;
        const std::filesystem::path dir = appDataDir() / L"updates";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) std::filesystem::remove(e.path(), ec);  // earlier downloads
        res->file = dir / wname;
        if (httpsDownload(L"github.com", path.c_str(), userAgent_.c_str(), res->file, kMaxInstaller, res->error, &session_)) {
            const SignatureCheck c = checkAuthenticode(res->file);
            if (isPinnedPublisher(c)) {
                res->ok = true;
            } else if (!c.trusted) {
                res->error = c.error;
            } else {
                res->error = "The installer is signed by \"" + narrow(c.commonName) + "\", not by " + narrow(kUpdatePublisher) + ".";
            }
            if (!res->ok) std::filesystem::remove(res->file, ec);
        }
        busy_ = false;
        if (!PostMessageW(hwnd_, installMessage_, 0, reinterpret_cast<LPARAM>(res))) delete res;
    });
    return true;
}

void Updater::stop() {
    session_.close();  // aborts a request in flight so the join below is immediate
    if (thread_.joinable()) thread_.join();
    hwnd_ = nullptr;
}

}  // namespace ixpat::win
