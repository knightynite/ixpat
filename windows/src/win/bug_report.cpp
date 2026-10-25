#include "win/bug_report.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>

#include "core/zip_writer.h"
#include "win/paths.h"

namespace ixpat::win {
namespace {

constexpr std::size_t kLogTail = 256u << 10;     // the last 256 KB of each log
constexpr std::size_t kMaxDump = 48u << 20;      // GitHub takes 25 MB per file; a Normal dump is far below either
constexpr const char* kIssuesUrl = "https://github.com/knightynite/ixpat/issues/new";

std::string readTail(const std::filesystem::path& p, std::size_t maxBytes) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) return {};
    const std::streamoff from = size > static_cast<std::streamoff>(maxBytes) ? size - static_cast<std::streamoff>(maxBytes) : 0;
    in.seekg(from);
    std::string s(static_cast<std::size_t>(size - from), '\0');
    in.read(s.data(), static_cast<std::streamsize>(s.size()));
    s.resize(static_cast<std::size_t>(in.gcount()));
    if (from > 0) {  // start at a whole line
        const auto nl = s.find('\n');
        if (nl != std::string::npos) s.erase(0, nl + 1);
    }
    return s;
}

std::string readAll(const std::filesystem::path& p, std::size_t maxBytes) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s;
    s.resize(maxBytes);
    in.read(s.data(), static_cast<std::streamsize>(maxBytes));
    s.resize(static_cast<std::size_t>(in.gcount()));
    return s;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::int64_t unixFromFileTime(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((u.QuadPart - 116444736000000000ull) / 10000000ull);
}

}  // namespace

std::string windowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")); fn && fn(&v) == 0) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
            return buf;
        }
    }
    return "unknown";
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (const unsigned char c : s) {
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (keep) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::vector<std::filesystem::path> crashDumps() {
    std::vector<std::filesystem::path> dumps;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(appDataDir(), ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (name.rfind(L"crash-", 0) == 0 && e.path().extension() == L".dmp") dumps.push_back(e.path());
    }
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) { return a.filename().wstring() > b.filename().wstring(); });  // the name carries the time
    return dumps;
}

std::int64_t newestCrashDumpTime() {
    const auto dumps = crashDumps();
    if (dumps.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(dumps.front().c_str(), GetFileExInfoStandard, &a)) return 0;
    return unixFromFileTime(a.ftLastWriteTime);
}

std::optional<std::filesystem::path> writeReportBundle(const ReportFacts& facts, std::string& error) {
    error.clear();
    const std::filesystem::path dir = appDataDir();
    SYSTEMTIME st;
    GetLocalTime(&st);
    const auto dosDate = static_cast<std::uint16_t>(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
    const auto dosTime = static_cast<std::uint16_t>((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    const auto dumps = crashDumps();

    std::string summary = "iXpat issue report\n";
    {
        SYSTEMTIME utc;
        GetSystemTime(&utc);
        char when[40];
        std::snprintf(when, sizeof when, "%04u-%02u-%02uT%02u:%02u:%02uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
        summary += std::string("Written: ") + when + "\n";
    }
    summary += "iXpat: " + facts.version + " for Windows\n";
    summary += "Windows: " + windowsBuild() + "\n";
    summary += "Surface Mouse: " + facts.device + "\n";
    summary += "Data folder: " + narrow(dir.wstring()) + "\n";
    summary += "Crash reports on disk: " + std::to_string(dumps.size()) + "\n";
    for (const auto& d : dumps) summary += "  " + narrow(d.filename().wstring()) + "\n";
    summary += facts.includeDump && !dumps.empty() ? "Included: the newest crash report, the logs, settings without the bridge token\n"
                                                    : "Included: the logs, settings without the bridge token; no crash report\n";

    ZipWriter zip;
    zip.add("summary.txt", summary.data(), summary.size(), dosDate, dosTime);
    for (const wchar_t* log : {L"debug.log", L"reports.log"}) {
        const std::string tail = readTail(dir / log, kLogTail);
        zip.add(narrow(log), tail.data(), tail.size(), dosDate, dosTime);
    }
    {
        std::string settings = readAll(settingsPath(), 4u << 20);
        settings = std::regex_replace(settings, std::regex(R"re("bridgeToken"\s*:\s*"[^"]*")re"), R"("bridgeToken": "")");
        zip.add("settings.json", settings.data(), settings.size(), dosDate, dosTime);
    }
    if (facts.includeDump && !dumps.empty()) {
        const std::string dump = readAll(dumps.front(), kMaxDump);
        if (dump.empty()) {
            error = "The crash report could not be read.";
            return std::nullopt;
        }
        zip.add(narrow(dumps.front().filename().wstring()), dump.data(), dump.size(), dosDate, dosTime);
    }

    const std::filesystem::path outDir = dir / L"reports";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    wchar_t name[64];
    _snwprintf_s(name, _TRUNCATE, L"ixpat-report-%04u%02u%02u-%02u%02u%02u.zip", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::filesystem::path out = outDir / name;
    const auto bytes = zip.finish();
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "The bundle could not be written to " + narrow(outDir.wstring()) + ".";
        std::filesystem::remove(out, ec);
        return std::nullopt;
    }
    return out;
}

std::string issueUrl(const ReportFacts& facts, const std::filesystem::path* bundle) {
    std::string body = "### What happened\n\n(What did you do, what did you expect, and what happened instead?)\n\n### Details\n\n";
    body += "- iXpat " + facts.version + " for Windows\n";
    body += "- Windows " + windowsBuild() + "\n";
    body += "- Surface Mouse: " + facts.device + "\n";
    if (bundle) {
        body += "- Report bundle: `" + narrow(bundle->filename().wstring()) + "`, attached below from the folder that opened (the logs, the settings without the bridge token" +
                std::string(facts.includeDump ? ", and the crash report" : "; no crash report") + ")\n";
    }
    return std::string(kIssuesUrl) + "?body=" + urlEncode(body);
}

}  // namespace ixpat::win
