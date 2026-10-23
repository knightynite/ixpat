#include "win/paths.h"

#include <windows.h>
#include <shlobj.h>

#include <system_error>

namespace ixpat::win {

std::filesystem::path appDataDir() {
    static const std::filesystem::path dir = [] {
        // IXPAT_DATA_DIR: a portable install or the test suite keeps its files somewhere else.
        wchar_t over[MAX_PATH];
        const DWORD on = GetEnvironmentVariableW(L"IXPAT_DATA_DIR", over, MAX_PATH);
        if (on && on < MAX_PATH) {
            std::filesystem::path d(over);
            std::error_code ec;
            std::filesystem::create_directories(d, ec);
            return d;
        }
        std::filesystem::path base;
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p)) && p) {
            base = p;
            CoTaskMemFree(p);
        } else {
            wchar_t buf[MAX_PATH];
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
            base = (n && n < MAX_PATH) ? std::filesystem::path(buf) : std::filesystem::temp_directory_path();
        }
        std::filesystem::path d = base / L"iXpat";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

std::filesystem::path settingsPath() { return appDataDir() / L"settings.json"; }

}  // namespace ixpat::win
