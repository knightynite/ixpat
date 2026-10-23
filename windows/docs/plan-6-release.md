# iXpat for Windows, Plan 6: updater, login item, installer, docs and the first release

**Goal:** Ship `win-v0.2.0`: a zip and a per-user installer on GitHub Releases, an in-app update check, start at login, an About window, and the README / website / plan doc telling Windows users what they get.

**Architecture:** `src/win/updater.h/.cpp` (WinHTTP, `GET /repos/knightynite/ixpat/releases?per_page=20`, picks the newest `win-v*` tag, compares against `kVersion` with `core/version.h`'s `compareVersions`; never downloads or runs anything: it opens the release page, fail-closed, because the binaries are unsigned). `src/win/login_item.h/.cpp` (HKCU `Software\Microsoft\Windows\CurrentVersion\Run`, value `iXpat` = quoted exe path). About becomes a `GfxWindow` pane in the settings window's header (version, build, links, "Check for updates"). Packaging: `windows/installer/ixpat.iss` (Inno Setup, per-user, `{autopf}` under `{localappdata}\Programs\iXpat`, optional login item, closes the running app), CI zips `iXpat.exe` + `README-windows.txt` per architecture and builds the installer on `win-v*` tags, attaching all three assets to a **pre-release** so the Mac updater's `releases/latest` keeps ignoring Windows tags.

**Tech Stack:** winhttp, advapi32, Inno Setup 6 (CI: `choco install innosetup`), GitHub Actions `softprops/action-gh-release`.

**Spec:** `windows/docs/design.md` sections 5 (packaging) and 6 (updates).

## Global Constraints
- Release tags: `win-vX.Y.Z`, marked pre-release. Assets: `iXpat-<ver>-windows-x64.zip`, `iXpat-<ver>-windows-arm64.zip`, `iXpat-Setup-<ver>-x64.exe`.
- The updater sends no identifying data (no token, a plain `User-Agent: iXpat-Windows/<ver>`), checks at most once per 24 h when `autoCheckUpdates` is on, and never auto-installs.
- The Mac app and website keep working untouched: `docs/index.html` gains a Windows section; nothing under `Sources/` changes.

## Tasks
1. `core/version.h`: `compareVersions(a, b)` (numeric dotted compare, `win-v` / `v` prefixes stripped) with tests. Commit `windows: version compare`.
2. `win/updater`: release list parse (`parseReleases(json) -> optional<ReleaseInfo{tag, version, url}>` in core, tested on a fixture), WinHTTP fetch on a worker thread, result posted to main (`WM_APP_UPDATE`), 24 h cadence persisted in settings (`windows.lastUpdateCheck`), "Check for updates" in the tray menu and About. Commit `windows: update check`.
3. `win/login_item`: `isEnabled()/setEnabled(bool)`, wired to `windows.startAtLogin` (settings toggle in Pointer & Scroll → "Startup"). Commit `windows: start at login`.
4. About: header "About" button → modal-less D2D pane with version, "Check for updates", GitHub and website links, licence. Commit `windows: about pane`.
5. Packaging: `windows/installer/ixpat.iss`, `windows/README-windows.txt`, CI release job. Commit `windows: installer and release packaging`.
6. Docs: README Windows sections + badges, `docs/index.html` Windows section and download, `docs/windows-plan.md` rewritten (status, CFU finding, what differs), `windows/docs/measurements-2026-10.md` pointer. Commit `docs: windows`.
7. Release: push `windows-port`, open PR to main, tag `win-v0.2.0` after merge; verify the assets, the website and that the Mac updater still resolves the Mac release as latest.
