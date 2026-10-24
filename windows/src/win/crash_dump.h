#pragma once

namespace ixpat::win {

/// The process-wide last-chance handler: an unhandled exception, a stack overflow, abort(), an uncaught C++
/// exception, std::terminate, a CRT invalid parameter or a pure virtual call writes a minidump beside the logs
/// (`crash-<time>-<pid>.dmp`, the last five kept) and a line in debug.log, then the process ends as it would
/// have. A dump thread and the paths are prepared here, so the fatal path allocates nothing and takes no lock.
/// Call once, first thing, on the main thread.
void installCrashHandler();

/// Reserves stack on the calling thread for the handler to run after a stack overflow. The main thread gets it
/// from installCrashHandler(); call it at the start of any other long-lived thread.
void reserveStackForCrashHandler();

/// Dies on purpose (`--crash[=abort|throw|overflow|param]`): proves the handler and the dump on a given
/// machine. `kind` is the text after the `=`, or empty for an access violation.
[[noreturn]] void crashNow(const wchar_t* kind);

}  // namespace ixpat::win
