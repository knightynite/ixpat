#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ixpat::win {

/// What an issue report carries besides the logs.
struct ReportFacts {
    std::string version;       // "0.2.9 (x64)"
    std::string device;        // "connected, firmware 5.29, battery 80%" or "not connected"
    bool includeDump = false;  // the newest crash dump goes in: the user agreed to share it
};

/// The crash dumps beside the logs (crash-<time>-<pid>.dmp), newest first.
std::vector<std::filesystem::path> crashDumps();
/// The write time of the newest dump as unix seconds, or 0 when there is none.
std::int64_t newestCrashDumpTime();

/// Writes <data dir>\reports\ixpat-report-<time>.zip: summary.txt (the facts, the Windows build, the dumps on disk),
/// the tails of debug.log and reports.log, settings.json with its bridge token blanked, and the newest dump when
/// asked for. Returns the path, or nothing with `error` filled. Nothing is sent anywhere.
std::optional<std::filesystem::path> writeReportBundle(const ReportFacts& facts, std::string& error);

/// GitHub's new-issue page with the facts filled in and, when a bundle was written, a line asking to attach it.
std::string issueUrl(const ReportFacts& facts, const std::filesystem::path* bundle);

/// "10.0.26100" from the kernel, not from the compatibility shims.
std::string windowsBuild();
/// Percent-encodes a URL query value (RFC 3986 unreserved characters pass, everything else is %XX).
std::string urlEncode(const std::string& s);

}  // namespace ixpat::win
