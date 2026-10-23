#pragma once
#include <string>

namespace ixpat::win {

/// Start at login through HKCU\Software\Microsoft\Windows\CurrentVersion\Run (the Mac uses SMAppService). Per-user,
/// no elevation; Task Manager's Startup tab can veto it without touching the value.
std::wstring loginItemCommand();  // this executable's quoted path
bool loginItemEnabled(const wchar_t* valueName = L"iXpat");
bool setLoginItem(bool on, const wchar_t* valueName = L"iXpat");

}  // namespace ixpat::win
