#include "win/login_item.h"

#include <windows.h>

namespace ixpat::win {
namespace {
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

std::wstring loginItemCommand() {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};  // failed or truncated: no command rather than a wrong one
    return L"\"" + std::wstring(exe, n) + L"\"";
}

bool loginItemEnabled(const wchar_t* valueName) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    const LSTATUS st = RegQueryValueExW(k, valueName, nullptr, &type, nullptr, &size);
    RegCloseKey(k);
    return st == ERROR_SUCCESS && type == REG_SZ;
}

bool setLoginItem(bool on, const wchar_t* valueName) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS st;
    if (on) {
        const std::wstring cmd = loginItemCommand();
        if (cmd.empty()) {
            RegCloseKey(k);
            return false;
        }
        st = RegSetValueExW(k, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(k, valueName);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

}  // namespace ixpat::win
