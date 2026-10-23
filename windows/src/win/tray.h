#pragma once
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace ixpat::win {

/// The notification-area icon (the Mac's menu bar extra). Callback messages arrive on `hwnd` as
/// `message`; the owner shows the menu. Re-adds itself after Explorer restarts (WM_TASKBARCREATED).
class Tray {
public:
    bool add(HWND hwnd, UINT message, HICON icon, const std::wstring& tip);
    void setTip(const std::wstring& tip);
    void setIcon(HICON icon);
    void remove();
    void onTaskbarCreated();
    static UINT taskbarCreatedMessage();

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace ixpat::win
