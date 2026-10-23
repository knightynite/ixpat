# Plan 6 review (independent review, 2026-10-23) and the triage

Read-only review of the release plumbing: the updater, the login item, the About pane, the installer, the CI release
job and the docs. Every finding was checked against the code before acting.

## Findings and what happened

| # | Severity | Finding | Verdict | Action |
|---|---|---|---|---|
| 1 | Important | The release page URL came from the JSON body (`html_url`) and went straight to `ShellExecuteW`; a tampered value could launch a file, a UNC path or a protocol handler | confirmed | The URL is built from the tag (`canonicalReleaseUrl`), and only a well-formed `win-v` tag qualifies (`isWindowsReleaseTag`, tested); About links are checked against the project's own destinations |
| 2 | Important | WinHTTP timeouts are per operation; a trickling response could hold the shutdown join for far longer than 15 s | confirmed | The session handle lives in a shared slot that `stop()` closes, which aborts the request at once |
| 3 | Important | The uninstaller's `taskkill /F` skipped the settings flush and pointer restore, and could hit other processes of the same name | confirmed | `iXpat.exe --quit` asks the running instance to leave gracefully; Restart Manager covers the rest |
| 4 | Important | The HKCU Run value was removed at uninstall only when the installer's task had written it; a value set in-app survived | confirmed | A second registry entry registers the deletion regardless of the task |
| 5 | Minor | Under `IXPAT_ANY_MOUSE`, switching start-at-login off never deleted the value | confirmed | Development runs never touch the Run key in either direction |
| 6 | Minor | Any `REG_SZ` counts as enabled, even a stale path after moving a portable install | plausible | Deferred: the launch-time adoption rule keeps the registry as the truth; a stale command still launches the old path, which the user can see in Task Manager |
| 7 | Minor | A failed or truncated `GetModuleFileNameW` wrote an empty command | confirmed | Empty command: no write, `setLoginItem` reports failure |
| 8 | Minor | The 24 h cadence applies only after a success; failures retry every 6 h | confirmed (shared with the Mac) | Deferred: a failed check every 6 h is one tiny request |
| 9 | Minor | The update-state file was truncated in place; an interrupted write lost the skipped version | confirmed | Temp file + `MoveFileEx` replace |
| 10 | Minor | An HTTP 200 with a non-list body counted as "no Windows release" and silenced checks for a day | confirmed | A body that is not a JSON array is reported as an error |
| 11 | Minor | Only 30 releases were examined; many Mac releases could hide the newest Windows one | confirmed | `per_page=100` (the API maximum); pagination deferred until the repo has more than 100 releases |
| 12 | Minor | The tray's About still opened the old message box; the About pane was never reached from the app | confirmed | Tray and settings About open the pane; the message box remains only for `IXPAT_NO_WINDOW` |
| 13 | Minor | `actions/checkout`, `upload-artifact` and `download-artifact` were on mutable `@v4` tags; no workflow-level read-only permissions | confirmed | All actions pinned to commit SHAs; `permissions: contents: read` at the workflow level, write only in the release job |
| 14 | Minor | `docs/windows-plan.md` claimed the app points users at Windows Update, which no UI does | confirmed | Claim narrowed |
| 15 | Fidelity | Manual checks did not offer "Skip this version"; the Mac does | confirmed | Offered on every check |

Also confirmed by the review: WinHTTP handles unwind in the right order, TLS and certificate validation stay on,
the 4 MiB body cap and proxy selection are sound, the release job's tag guard keeps fork pull requests away from the
write-enabled job, pre-releases protect the Mac app's `releases/latest`, and the zip contents match the docs.

Missing tests the reviewer asked for: release trust boundary (now `test_updater.cpp`: executable / off-site URLs
never open because the page is built from a validated tag), updater shutdown under a trickling response (manual),
and the installer / startup matrix (manual).
