#pragma once
#include <windows.h>

#include <functional>
#include <optional>
#include <string>

#include "core/actions.h"

namespace ixpat::win {

/// Runs a ButtonAction on Windows (the Mac's ActionRunner). Ring and gesture kinds are the router's.
void runAction(const ButtonAction& a);

/// Default output volume 0..1 (IAudioEndpointVolume), for the "already at the limit" bump. Main thread, COM initialised.
std::optional<float> systemVolume();

/// Foreground window changes -> (image file name like "chrome.exe", pid). Callback on the main thread
/// (SetWinEventHook out-of-context needs a message loop there).
class ForegroundWatch {
public:
    void start(std::function<void(std::string imageName, DWORD pid)> onChange);
    void stop();
    static std::string imageNameOf(HWND hwnd, DWORD* pidOut = nullptr);

private:
    static void CALLBACK proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static ForegroundWatch* instance_;
    HWINEVENTHOOK hook_ = nullptr;
    std::function<void(std::string, DWORD)> onChange_;
};

/// The foreground window's visible frame (DWM extended bounds), for snap detection.
std::optional<RECT> foregroundFrame();
/// The work area of the monitor that holds `frame`.
std::optional<RECT> workAreaFor(const RECT& frame);

/// Pointer speed / acceleration / wheel lines while the Surface Mouse is the active device; restores
/// the system values. Nothing is written to the user profile (no SPIF_UPDATEINIFILE).
class PointerSettings {
public:
    void apply(double speed0to3, bool noAcceleration, double scrollSpeed);
    void restore();
    bool applied() const { return applied_; }

private:
    bool saved_ = false, applied_ = false;
    int origSpeed_ = 10;
    int origMouse_[3] = {6, 10, 1};
    UINT origLines_ = 3;
};

}  // namespace ixpat::win
