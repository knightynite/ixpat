#include "win/crash_dump.h"

#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "win/log.h"
#include "win/paths.h"

namespace ixpat::win {
namespace {

constexpr int kKeepDumps = 5;
wchar_t gDir[MAX_PATH];  // resolved at install: nothing is allocated once things have gone wrong

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep) {
    static wchar_t path[MAX_PATH + 64];
    SYSTEMTIME st;
    GetSystemTime(&st);
    _snwprintf_s(path, _TRUNCATE, L"%s\\crash-%04u%02u%02u-%02u%02u%02u.dmp", gDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool written = false;
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithThreadInfo);
        written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, ep ? &mei : nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(h);
    }
    // The log line comes after the dump is safe: it allocates, and the heap may be what broke.
    char line[256];
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* at = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    std::snprintf(line, sizeof line, "crash: unhandled exception 0x%08lX at %p; minidump %s", static_cast<unsigned long>(code), at,
                  written ? "written beside this log" : "could not be written");
    debugLog(line);
    return EXCEPTION_EXECUTE_HANDLER;  // the process ends as it would have; Windows Error Reporting may still note it
}

}  // namespace

void installCrashHandler() {
    const std::filesystem::path dir = appDataDir();
    wcsncpy_s(gDir, dir.c_str(), _TRUNCATE);
    // Keep the last few dumps only: a crash loop must not fill the disk.
    std::vector<std::filesystem::path> dumps;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const auto name = e.path().filename().wstring();
        if (name.rfind(L"crash-", 0) == 0 && e.path().extension() == L".dmp") dumps.push_back(e.path());
    }
    std::sort(dumps.begin(), dumps.end());  // the names sort by time
    while (dumps.size() >= static_cast<std::size_t>(kKeepDumps)) {
        std::filesystem::remove(dumps.front(), ec);
        dumps.erase(dumps.begin());
    }
    SetUnhandledExceptionFilter(&onUnhandled);
}

void crashNow() {
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    ExitProcess(3);  // not reached: the filter above lets the process end
}

}  // namespace ixpat::win
