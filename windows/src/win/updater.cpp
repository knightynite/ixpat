#include "win/updater.h"

#include <winhttp.h>

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

#include "win/log.h"
#include "win/paths.h"

namespace ixpat::win {
namespace {

constexpr std::size_t kMaxBody = 4u << 20;

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

std::filesystem::path statePath() { return appDataDir() / L"update-state.json"; }

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
    return s;
}

void saveUpdateState(const UpdateState& s) {
    const nlohmann::json j{{"lastCheck", s.lastCheck}, {"skipped", s.skipped}};
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
    struct Handles {
        HINTERNET s = nullptr, c = nullptr, r = nullptr;
        SessionSlot* slot = nullptr;
        ~Handles() {
            if (r) WinHttpCloseHandle(r);
            if (c) WinHttpCloseHandle(c);
            if (slot) slot->close();  // the slot owns the session once published
            else if (s) WinHttpCloseHandle(s);
        }
    } h;
    h.s = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.s) h.s = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.s) {
        error = lastErrorText(GetLastError());
        return false;
    }
    if (slot) {
        h.slot = slot;
        slot->set(h.s);
    }
    WinHttpSetTimeouts(h.s, 10000, 10000, 10000, 15000);
    h.c = WinHttpConnect(h.s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!h.c) {
        error = lastErrorText(GetLastError());
        return false;
    }
    h.r = WinHttpOpenRequest(h.c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!h.r) {
        error = lastErrorText(GetLastError());
        return false;
    }
    WinHttpAddRequestHeaders(h.r, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(h.r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(h.r, nullptr)) {
        error = lastErrorText(GetLastError());
        return false;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(h.r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(h.r, &avail)) {
            error = lastErrorText(GetLastError());
            return false;
        }
        if (avail == 0) break;
        if (body.size() + avail > kMaxBody) {
            error = "The response was too large.";
            return false;
        }
        const std::size_t off = body.size();
        body.resize(off + avail);
        DWORD got = 0;
        if (!WinHttpReadData(h.r, body.data() + off, avail, &got)) {
            error = lastErrorText(GetLastError());
            return false;
        }
        body.resize(off + got);
        if (got == 0) break;
    }
    if (status != 200) {
        error = status == 404 ? "No release has been published yet." : "GitHub answered HTTP " + std::to_string(status) + ".";
        return false;
    }
    return true;
}

Updater::~Updater() { stop(); }

void Updater::init(HWND hwnd, UINT message, std::wstring userAgent) {
    hwnd_ = hwnd;
    message_ = message;
    userAgent_ = std::move(userAgent);
}

bool Updater::check(bool manual) {
    if (busy_.load() || !hwnd_) return false;
    if (thread_.joinable()) thread_.join();  // the previous check finished; reclaim it
    busy_ = true;
    AcquireSRWLockExclusive(&session_.lock);
    session_.handle = nullptr;
    session_.closed = false;
    ReleaseSRWLockExclusive(&session_.lock);
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
        if (!PostMessageW(hwnd_, message_, manual ? 1 : 0, reinterpret_cast<LPARAM>(res))) delete res;
    });
    return true;
}

void Updater::stop() {
    session_.close();  // aborts a request in flight so the join below is immediate
    if (thread_.joinable()) thread_.join();
    hwnd_ = nullptr;
}

}  // namespace ixpat::win
