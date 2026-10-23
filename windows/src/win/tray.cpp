#include "win/tray.h"

#include <cstring>

namespace ixpat::win {

bool Tray::add(HWND hwnd, UINT message, HICON icon, const std::wstring& tip) {
    data_ = NOTIFYICONDATAW{};
    data_.cbSize = sizeof data_;
    data_.hWnd = hwnd;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = message;
    data_.hIcon = icon;
    wcsncpy_s(data_.szTip, tip.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != 0;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

void Tray::setTip(const std::wstring& tip) {
    if (!added_) return;
    wcsncpy_s(data_.szTip, tip.c_str(), _TRUNCATE);
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void Tray::setIcon(HICON icon) {
    if (!added_) return;
    data_.hIcon = icon;
    data_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void Tray::remove() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void Tray::onTaskbarCreated() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_ADD, &data_);
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
}

UINT Tray::taskbarCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

}  // namespace ixpat::win
