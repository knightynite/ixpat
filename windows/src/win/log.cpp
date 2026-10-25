#include "win/log.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <system_error>

#include "win/paths.h"

namespace ixpat::win {

FileLog::FileLog(std::filesystem::path file) : file_(std::move(file)) {}

void FileLog::write(std::string_view line) {
    using namespace std::chrono;
    const auto now = floor<milliseconds>(system_clock::now());
    const auto days = floor<std::chrono::days>(now);
    const year_month_day ymd{days};
    const hh_mm_ss hms{now - days};
    char stamp[40];
    std::snprintf(stamp, sizeof stamp, "%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ ", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<long long>(hms.hours().count()), static_cast<long long>(hms.minutes().count()),
                  static_cast<long long>(hms.seconds().count()), static_cast<long long>(hms.subseconds().count()));
    std::string out = stamp;
    out.append(line);
    out.push_back('\n');

    AcquireSRWLockExclusive(&lock_);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file_, ec);
    if (!ec && size > kLimit) {
        std::filesystem::path old = file_;
        old += L".1";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(file_, old, ec);
    }
    HANDLE h = CreateFileW(file_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
        CloseHandle(h);
    }
    ReleaseSRWLockExclusive(&lock_);
}

void debugLog(std::string_view s) {
    static FileLog log(appDataDir() / L"debug.log");
    log.write(s);
}

void reportLog(std::string_view s) {
    static FileLog log(appDataDir() / L"reports.log");
    log.write(s);
}

long long qpcTicks() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

double msSince(long long qpc) {
    static const double perMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(qpcTicks() - qpc) / perMs;
}

void debugLogSlow(const char* what, double ms) {
    thread_local long long windowStart = 0;  // per thread: the input and injector threads both call this
    thread_local int inWindow = 0;
    const long long now = qpcTicks();
    if (msSince(windowStart) > 1000) {
        windowStart = now;
        inWindow = 0;
    }
    if (++inWindow > 4) return;
    char line[160];
    std::snprintf(line, sizeof line, "slow: %s took %.1f ms", what, ms);
    debugLog(line);
}

}  // namespace ixpat::win
