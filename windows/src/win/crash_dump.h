#pragma once

namespace ixpat::win {

/// The process-wide last-chance handler: an unhandled exception writes a minidump beside the logs
/// (`crash-<time>.dmp`, the last five kept) and a line in debug.log, then the process ends as it would have.
/// Call once, first thing.
void installCrashHandler();

/// Raises an access violation on purpose (`--crash`): proves the handler and the dump on a given machine.
[[noreturn]] void crashNow();

}  // namespace ixpat::win
