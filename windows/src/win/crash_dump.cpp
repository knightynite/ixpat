#include "win/crash_dump.h"

#include <windows.h>
#include <crtdbg.h>
#include <dbghelp.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "win/log.h"
#include "win/paths.h"

namespace ixpat::win {
namespace {

constexpr int kKeepDumps = 5;
// Our own codes for the CRT's ways of dying, so they reach the same filter with a context.
constexpr DWORD kCodeTerminate = 0xE0495801;         // std::terminate: a noexcept violation or a direct call
constexpr DWORD kCodeAbort = 0xE0495802;             // abort(), also assert() in a debug build
constexpr DWORD kCodeInvalidParameter = 0xE0495803;  // a CRT function given an invalid argument
constexpr DWORD kCodePureCall = 0xE0495804;          // a pure virtual call during construction or destruction

// Everything the fatal path touches is prepared while the process is healthy. Once something has gone wrong
// there is no allocation, no logger (its lock may be the one the crashing thread holds) and no CRT lock.
wchar_t gDir[MAX_PATH];   // empty: no place to write a dump
wchar_t gLog[MAX_PATH];   // debug.log, appended to directly
HANDLE gGo = nullptr;     // the dump thread waits on this
HANDLE gDone = nullptr;   // and signals this once the dump and its log line are written
EXCEPTION_POINTERS* gPointers = nullptr;
DWORD gCrashThread = 0;
LONG volatile gEntered = 0;

const char* nameOf(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case 0xE06D7363: return "uncaught C++ exception";
        case kCodeTerminate: return "std::terminate";
        case kCodeAbort: return "abort";
        case kCodeInvalidParameter: return "CRT invalid parameter";
        case kCodePureCall: return "pure virtual call";
        default: return "exception";
    }
}

// The logger's line format without the logger: "<ISO-8601 UTC> <text>\n", appended with one Win32 call.
void appendLogLine(const char* text) {
    if (gLog[0] == 0) return;
    SYSTEMTIME st;
    GetSystemTime(&st);
    char line[320];
    const int n = std::snprintf(line, sizeof line, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                                st.wMinute, st.wSecond, st.wMilliseconds, text);
    if (n <= 0) return;
    for (int attempt = 0; attempt < 5; ++attempt) {  // the logger may be mid-write on another thread
        HANDLE h = CreateFileW(gLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, line, static_cast<DWORD>(n), &written, nullptr);
            CloseHandle(h);
            return;
        }
        Sleep(20);
    }
}

// Runs on the dump thread: the crashing thread may have no stack left (a stack overflow) and its frames must
// stay as they are for the dump.
void writeDumpAndLog() {
    static wchar_t path[MAX_PATH + 96];
    bool written = false;
    if (gDir[0] != 0) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        const unsigned long pid = GetCurrentProcessId();
        CreateDirectoryW(gDir, nullptr);  // a first run may fail before anything else has made the folder
        HANDLE h = INVALID_HANDLE_VALUE;
        for (int seq = 0; seq < 10 && h == INVALID_HANDLE_VALUE; ++seq) {  // CREATE_NEW: never over an earlier dump
            if (seq == 0)
                _snwprintf_s(path, _TRUNCATE, L"%s\\crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp", gDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, pid);
            else
                _snwprintf_s(path, _TRUNCATE, L"%s\\crash-%04u%02u%02u-%02u%02u%02u-%lu-%d.dmp", gDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, pid, seq);
            h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
        }
        if (h != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{gCrashThread, gPointers, FALSE};
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithThreadInfo);
            written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, gPointers ? &mei : nullptr, nullptr, nullptr) != FALSE;
            CloseHandle(h);
        }
    }
    char line[256];
    const EXCEPTION_RECORD* rec = gPointers ? gPointers->ExceptionRecord : nullptr;
    const DWORD code = rec ? rec->ExceptionCode : 0;
    std::snprintf(line, sizeof line, "crash: %s 0x%08lX at %p on thread %lu; minidump %s", nameOf(code), static_cast<unsigned long>(code),
                  rec ? rec->ExceptionAddress : nullptr, static_cast<unsigned long>(gCrashThread),
                  written ? "written beside this log" : (gDir[0] ? "could not be written" : "has no folder"));
    appendLogLine(line);
}

DWORD WINAPI dumpThread(void*) {
    if (WaitForSingleObject(gGo, INFINITE) == WAIT_OBJECT_0) {
        writeDumpAndLog();
        SetEvent(gDone);
    }
    return 0;
}

DWORD WINAPI dumpOnce(void*) {
    writeDumpAndLog();
    return 0;
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep) {
    if (InterlockedCompareExchange(&gEntered, 1, 0) != 0) {  // another thread is being dumped; the process ends with it
        Sleep(30000);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    gPointers = ep;
    gCrashThread = GetCurrentThreadId();
    if (gGo && SetEvent(gGo)) {
        WaitForSingleObject(gDone, 30000);
    } else {  // no dump thread was to be had at install: try one now, then in place
        HANDLE t = CreateThread(nullptr, 256 * 1024, &dumpOnce, nullptr, 0, nullptr);
        if (t) {
            WaitForSingleObject(t, 30000);
            CloseHandle(t);
        } else {
            writeDumpAndLog();
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;  // the process ends as it would have
}

// The CRT's exits bypass the unhandled-exception filter (they fail fast), so each is turned into an exception
// first: the filter then has the thread's context and the dump shows where it happened.
[[noreturn]] void raiseAndDie(DWORD code) {
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    ExitProcess(3);  // only if a debugger let the exception continue
}
void onTerminate() { raiseAndDie(kCodeTerminate); }
void onAbort(int) { raiseAndDie(kCodeAbort); }
void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { raiseAndDie(kCodeInvalidParameter); }
void onPureCall() { raiseAndDie(kCodePureCall); }

// --crash=overflow: recursion through a volatile pointer, which the compiler cannot flatten or flag.
using Recurse = int (*)(int);
Recurse volatile gRecurse = nullptr;
int recurse(int depth) {
    volatile char pad[512];
    pad[0] = static_cast<char>(depth);
    return gRecurse(depth + 1) + pad[0];
}

}  // namespace

void reserveStackForCrashHandler() {
    ULONG guarantee = 32 * 1024;
    SetThreadStackGuarantee(&guarantee);
}

void installCrashHandler() {
    std::string note;  // logged the normal way once the handler is in place
    try {
        const std::filesystem::path dir = appDataDir();
        const std::wstring d = dir.native();
        if (d.size() + 96 < MAX_PATH) {  // room for a dump's name
            wcscpy_s(gDir, d.c_str());
            _snwprintf_s(gLog, _TRUNCATE, L"%s\\debug.log", gDir);
        } else {
            note = "crash dumps unavailable: the data folder's path is too long";
        }
        // Keep the last few dumps only: a crash loop must not fill the disk.
        std::vector<std::filesystem::path> dumps;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().wstring();
            if (name.rfind(L"crash-", 0) == 0 && it->path().extension() == L".dmp") dumps.push_back(it->path());
        }
        std::sort(dumps.begin(), dumps.end());  // the names begin with the UTC time
        int failed = 0;
        while (dumps.size() > static_cast<std::size_t>(kKeepDumps)) {
            ec.clear();
            std::filesystem::remove(dumps.front(), ec);
            if (ec) ++failed;
            dumps.erase(dumps.begin());
        }
        if (!dumps.empty()) {
            note = std::to_string(dumps.size()) + " crash dump(s) present, newest " + dumps.back().filename().string();
            if (failed) note += "; " + std::to_string(failed) + " older one(s) could not be removed";
        }
    } catch (...) {
        note = "crash dumps unavailable: the data folder could not be resolved";
    }
    gGo = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (gGo && gDone) {
        if (HANDLE t = CreateThread(nullptr, 256 * 1024, &dumpThread, nullptr, 0, nullptr)) {
            CloseHandle(t);
        } else {
            CloseHandle(gGo);
            CloseHandle(gDone);
            gGo = gDone = nullptr;
        }
    }
    reserveStackForCrashHandler();
    SetUnhandledExceptionFilter(&onUnhandled);
    std::set_terminate(&onTerminate);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);  // no message box, no fail-fast: the signal below
    std::signal(SIGABRT, &onAbort);
    _set_invalid_parameter_handler(&onInvalidParameter);
    _set_purecall_handler(&onPureCall);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);  // the debug CRT's assertion dialogs would block the dump
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif
    if (!note.empty()) debugLog(note);
}

void crashNow(const wchar_t* kind) {
    if (wcscmp(kind, L"abort") == 0) std::abort();
    if (wcscmp(kind, L"throw") == 0) throw std::runtime_error("--crash=throw");
    if (wcscmp(kind, L"overflow") == 0) {
        gRecurse = &recurse;
        recurse(0);
    }
    if (wcscmp(kind, L"param") == 0) {
        char buf[4];
        strcpy_s(buf, sizeof buf, "too long for the buffer");  // the CRT reports an invalid parameter
    }
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    ExitProcess(3);  // not reached: the filter above lets the process end
}

}  // namespace ixpat::win
