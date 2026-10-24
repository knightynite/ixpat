#include "win/actions.h"

#include <dwmapi.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <shellapi.h>
#include <wbemidl.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "win/log.h"
#include "win/send_input.h"

namespace ixpat::win {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

/// Laptop panel brightness through WMI (root\WMI WmiMonitorBrightness / WmiMonitorBrightnessMethods).
/// External monitors are not reachable this way; the call then logs once and does nothing.
void stepBrightness(int delta) {
    ComPtr<IWbemLocator> loc;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<void**>(&loc)))) return;
    ComPtr<IWbemServices> svc;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    if (FAILED(hr)) return;
    if (FAILED(CoSetProxyBlanket(svc.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) return;
    // Current level
    ComPtr<IEnumWbemClassObject> en;
    BSTR wql = SysAllocString(L"WQL"), q = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
    hr = svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(wql);
    SysFreeString(q);
    if (FAILED(hr) || !en) return;
    ComPtr<IWbemClassObject> row;
    ULONG got = 0;
    if (en->Next(WBEM_INFINITE, 1, &row, &got) != S_OK || !got) {
        static bool logged = false;
        if (!logged) { logged = true; debugLog("brightness: no WMI-controllable panel (external monitors are not supported)"); }
        return;
    }
    VARIANT v;
    VariantInit(&v);
    int level = 50;
    if (SUCCEEDED(row->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr)) && v.vt == VT_UI1) level = v.bVal;
    VariantClear(&v);
    const int target = std::clamp(level + delta, 0, 100);
    // Instance path of the methods class
    ComPtr<IEnumWbemClassObject> en2;
    BSTR wql2 = SysAllocString(L"WQL"), q2 = SysAllocString(L"SELECT * FROM WmiMonitorBrightnessMethods");
    hr = svc->ExecQuery(wql2, q2, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en2);
    SysFreeString(wql2);
    SysFreeString(q2);
    if (FAILED(hr) || !en2) return;
    ComPtr<IWbemClassObject> methods;
    if (en2->Next(WBEM_INFINITE, 1, &methods, &got) != S_OK || !got) return;
    VARIANT path;
    VariantInit(&path);
    if (FAILED(methods->Get(L"__PATH", 0, &path, nullptr, nullptr))) return;
    ComPtr<IWbemClassObject> cls, inSig, params;
    BSTR clsName = SysAllocString(L"WmiMonitorBrightnessMethods");
    hr = svc->GetObject(clsName, 0, nullptr, &cls, nullptr);
    SysFreeString(clsName);
    if (SUCCEEDED(hr) && cls && SUCCEEDED(cls->GetMethod(L"WmiSetBrightness", 0, &inSig, nullptr)) && inSig &&
        SUCCEEDED(inSig->SpawnInstance(0, &params))) {
        VARIANT t, b;
        VariantInit(&t);
        VariantInit(&b);
        t.vt = VT_I4;
        t.lVal = 1;  // timeout seconds
        b.vt = VT_UI1;
        b.bVal = static_cast<BYTE>(target);
        params->Put(L"Timeout", 0, &t, 0);
        params->Put(L"Brightness", 0, &b, 0);
        BSTR method = SysAllocString(L"WmiSetBrightness");
        svc->ExecMethod(path.bstrVal, method, 0, nullptr, params.p, nullptr, nullptr);
        SysFreeString(method);
    }
    VariantClear(&path);
}

}  // namespace

void runAction(const ButtonAction& a) {
    switch (a.kind) {
        case ActionKind::none:
        case ActionKind::actionRing:
        case ActionKind::gestures:
            break;
        case ActionKind::missionControl: sendKeyChord({VK_LWIN, VK_TAB}); break;
        case ActionKind::spotlight: sendKeyChord({VK_LWIN, 'S'}); break;
        case ActionKind::screenshot: sendKeyChord({VK_LWIN, VK_SHIFT, 'S'}); break;
        case ActionKind::lockScreen: LockWorkStation(); break;
        case ActionKind::playPause: sendMediaKey(VK_MEDIA_PLAY_PAUSE); break;
        case ActionKind::showDesktop: sendKeyChord({VK_LWIN, 'D'}); break;
        case ActionKind::volumeUp: sendMediaKey(VK_VOLUME_UP); break;
        case ActionKind::volumeDown: sendMediaKey(VK_VOLUME_DOWN); break;
        case ActionKind::mute: sendMediaKey(VK_VOLUME_MUTE); break;
        case ActionKind::nextTrack: sendMediaKey(VK_MEDIA_NEXT_TRACK); break;
        case ActionKind::previousTrack: sendMediaKey(VK_MEDIA_PREV_TRACK); break;
        case ActionKind::brightnessUp: stepBrightness(+10); break;
        case ActionKind::brightnessDown: stepBrightness(-10); break;
        case ActionKind::spaceLeft: sendKeyChord({VK_LWIN, VK_CONTROL, VK_LEFT}); break;
        case ActionKind::spaceRight: sendKeyChord({VK_LWIN, VK_CONTROL, VK_RIGHT}); break;
        case ActionKind::launchApp:
            if (!a.path.empty()) ShellExecuteW(nullptr, L"open", widen(a.path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ActionKind::shortcut: {
            if (a.keyCode < 0 || a.keyCode > 0xFE) break;
            WORD keys[5];
            int n = 0;
            if (a.flags & kModCtrl) keys[n++] = VK_CONTROL;
            if (a.flags & kModAlt) keys[n++] = VK_MENU;
            if (a.flags & kModShift) keys[n++] = VK_SHIFT;
            if (a.flags & kModWin) keys[n++] = VK_LWIN;
            keys[n++] = static_cast<WORD>(a.keyCode);
            switch (n) {
                case 1: sendKeyChord({keys[0]}); break;
                case 2: sendKeyChord({keys[0], keys[1]}); break;
                case 3: sendKeyChord({keys[0], keys[1], keys[2]}); break;
                case 4: sendKeyChord({keys[0], keys[1], keys[2], keys[3]}); break;
                default: sendKeyChord({keys[0], keys[1], keys[2], keys[3], keys[4]}); break;
            }
            break;
        }
        case ActionKind::shell: {
            if (a.command.empty()) break;
            std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command " + widen(a.command);
            STARTUPINFOW si{};
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            break;
        }
    }
}

std::optional<float> systemVolume() {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en)))) return std::nullopt;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return std::nullopt;
    ComPtr<IAudioEndpointVolume> vol;
    if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&vol)))) return std::nullopt;
    float level = 0;
    if (FAILED(vol->GetMasterVolumeLevelScalar(&level))) return std::nullopt;
    return level;
}

// MARK: foreground

ForegroundWatch* ForegroundWatch::instance_ = nullptr;

std::string ForegroundWatch::imageNameOf(HWND hwnd, DWORD* pidOut) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pidOut) *pidOut = pid;
    if (!pid) return {};
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t path[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(path));
    std::string name;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        std::wstring w(path, n);
        auto slash = w.find_last_of(L"\\/");
        std::wstring file = slash == std::wstring::npos ? w : w.substr(slash + 1);
        int len = WideCharToMultiByte(CP_UTF8, 0, file.data(), static_cast<int>(file.size()), nullptr, 0, nullptr, nullptr);
        name.resize(static_cast<std::size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, file.data(), static_cast<int>(file.size()), name.data(), len, nullptr, nullptr);
    }
    CloseHandle(p);
    return name;
}

void CALLBACK ForegroundWatch::proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || !hwnd || !instance_ || !instance_->onChange_) return;
    DWORD pid = 0;
    std::string name = imageNameOf(hwnd, &pid);
    if (!name.empty()) instance_->onChange_(std::move(name), pid);
}

void ForegroundWatch::start(std::function<void(std::string, DWORD)> onChange) {
    onChange_ = std::move(onChange);
    instance_ = this;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &ForegroundWatch::proc, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void ForegroundWatch::stop() {
    if (hook_) UnhookWinEvent(hook_);
    hook_ = nullptr;
    instance_ = nullptr;
}

std::optional<RECT> foregroundFrame() {
    HWND h = GetForegroundWindow();
    if (!h) return std::nullopt;
    RECT r{};
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) return r;
    if (GetWindowRect(h, &r)) return r;
    return std::nullopt;
}

std::optional<RECT> workAreaFor(const RECT& frame) {
    HMONITOR m = MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!m || !GetMonitorInfoW(m, &mi)) return std::nullopt;
    return mi.rcWork;
}

// MARK: pointer settings

void PointerSettings::apply(double speed0to3, bool noAcceleration, double scrollSpeed) {
    if (!saved_) {
        SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &origSpeed_, 0);
        SystemParametersInfoW(SPI_GETMOUSE, 0, origMouse_, 0);
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &origLines_, 0);
        saved_ = true;
    }
    // macOS tracking 0...3 -> Windows 1...20
    const int speed = std::clamp(static_cast<int>(std::lround(1 + std::clamp(speed0to3, 0.0, 3.0) / 3.0 * 19)), 1, 20);
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, reinterpret_cast<PVOID>(static_cast<INT_PTR>(speed)), SPIF_SENDCHANGE);
    int mouse[3] = {origMouse_[0], origMouse_[1], noAcceleration ? 0 : 1};
    SystemParametersInfoW(SPI_SETMOUSE, 0, mouse, SPIF_SENDCHANGE);
    // macOS scroll 0.05...1.5 (default 0.3125 = 3 lines) -> 1...12 lines
    const UINT lines = std::clamp(static_cast<UINT>(std::lround(scrollSpeed / 0.3125 * 3)), 1u, 12u);
    SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, lines, nullptr, SPIF_SENDCHANGE);
    applied_ = true;
}

void PointerSettings::restore() {
    if (!saved_ || !applied_) return;
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, reinterpret_cast<PVOID>(static_cast<INT_PTR>(origSpeed_)), SPIF_SENDCHANGE);
    SystemParametersInfoW(SPI_SETMOUSE, 0, origMouse_, SPIF_SENDCHANGE);
    SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, origLines_, nullptr, SPIF_SENDCHANGE);
    applied_ = false;
}

}  // namespace ixpat::win
