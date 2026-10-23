// iXpat for Windows: the wiring (the Mac AppDelegate). Everything else is a unit with one job.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "app/version.h"
#include "core/haptic_engine.h"
#include "core/router.h"
#include "core/settings.h"
#include "core/snap.h"
#include "win/actions.h"
#include "win/bridge_server.h"
#include "win/hid_device.h"
#include "win/input_thread.h"
#include "win/log.h"
#include "win/paths.h"
#include "win/settings_store.h"
#include "win/tray.h"
#include "ui/ring_window.h"

#define IDI_IXPAT 101

namespace {

using namespace ixpat;

constexpr UINT WM_APP_INPUT = WM_APP + 1;
constexpr UINT WM_APP_HID = WM_APP + 2;
constexpr UINT WM_APP_SETTINGS = WM_APP + 3;
constexpr UINT WM_APP_BRIDGE = WM_APP + 4;
constexpr UINT WM_APP_TRAY = WM_APP + 5;
constexpr UINT WM_APP_SHOW = WM_APP + 6;

constexpr UINT_PTR kTimerFlush = 4, kTimerWatchdog = 5, kTimerSnapCheck = 7, kTimerPointerApply = 8, kTimerRingCommit = 9,
                   kTimerDemoClose = 10;
constexpr UINT_PTR kFirstEchoTimer = 100;

enum MenuId : UINT { kMenuEnabled = 1, kMenuHaptics, kMenuOpenSettings, kMenuOpenLogs, kMenuReload, kMenuAbout, kMenuQuit };

const wchar_t* kWindowClass = L"iXpat.Main";
const wchar_t* kMutexName = L"Local\\iXpat.SurfaceMouse.SingleInstance";

struct BridgePlay {
    std::string name;
    int intensity;
};

class App {
public:
    int run(HINSTANCE inst, bool ringDemo);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void start();
    void shutdown();
    Seconds now() const;
    EngineParams engineParams() const;
    void pushConfig();
    void onInputEvents();
    void onDeviceChanged();
    void reloadSettings();
    void settingsChanged();
    void showMenu();
    void showAbout();
    void updateTray();
    void applyPointer();
    void startBridgeIfWanted();
    void snapCheck();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    double qpcFreq_ = 1;

    std::unique_ptr<win::SettingsStore> store_;
    Settings settings_;
    HapticEngine engine_;
    std::unique_ptr<Router> router_;
    win::HidDevice hid_;
    win::InputThread input_;
    win::ForegroundWatch foreground_;
    win::BridgeServer bridge_;
    win::PointerSettings pointer_;
    win::Tray tray_;
    ui::RingWindow ring_;
    std::optional<ButtonAction> pendingRingAction_;
    bool ringDemo_ = false;

    win::DeviceInfo device_;
    Seconds lastDetach_ = -1e9;
    bool anyMouse_ = false;
    bool lastFreeSpin_ = false;
    std::map<UINT_PTR, HapticEngine::Deferred> echoes_;
    UINT_PTR nextEcho_ = kFirstEchoTimer;
    std::optional<IRect> frameBeforeDrag_;
    std::uint32_t lastHeartbeat_ = 0, lastRaw_ = 0;
    std::string bridgeToken_;
    int bridgePort_ = 0;
};

Seconds App::now() const {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / qpcFreq_;
}

EngineParams App::engineParams() const {
    EngineParams p;
    p.enabled = settings_.enabled;
    p.effHapticsEnabled = settings_.effHapticsEnabled();
    p.masterIntensity = settings_.masterIntensity;
    return p;
}

LRESULT CALLBACK App::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

int App::run(HINSTANCE inst, bool ringDemo) {
    inst_ = inst;
    ringDemo_ = ringDemo;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFreq_ = static_cast<double>(f.QuadPart);

    // One instance: a second launch asks the first to show itself and exits.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) PostMessageW(other, WM_APP_SHOW, 0, 0);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    icon_ = LoadIconW(inst, MAKEINTRESOURCEW(IDI_IXPAT));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &App::wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = icon_;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kWindowClass, L"iXpat", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, inst, this);
    if (!hwnd_) return 1;

    start();
    if (ringDemo_) {  // --ring-demo: show the default ring for 3 s, then quit (a visual check without the mouse)
        ring_.open(settings_.effRingItems(), false, settings_.accentHex);
        SetTimer(hwnd_, kTimerDemoClose, 3000, nullptr);
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    shutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}

void App::start() {
    char env[8];
    anyMouse_ = GetEnvironmentVariableA("IXPAT_ANY_MOUSE", env, sizeof env) > 0 && env[0] == '1';
    win::debugLog(std::string("launch iXpat ") + IXPAT_VERSION_STRING +
#if defined(_M_ARM64)
                  " arm64"
#else
                  " x64"
#endif
                  + (anyMouse_ ? " IXPAT_ANY_MOUSE=1 (every pointing device counts as the Surface Mouse)" : ""));

    store_ = std::make_unique<win::SettingsStore>(win::settingsPath());
    settings_ = store_->load();
    // First launch: write the file so the bridge token is on disk for plugins (the Mac saves on init too).
    if (!std::filesystem::exists(win::settingsPath())) store_->save(settings_);
    store_->watch(hwnd_, WM_APP_SETTINGS);

    // Engine -> device; echoes come back through one-shot timers.
    engine_.trigger = [this](HapticEngine::Play p, std::string_view) { hid_.trigger(p.ordinal, p.intensity); };
    engine_.defer = [this](HapticEngine::Deferred d) {
        const UINT_PTR id = nextEcho_++;
        if (nextEcho_ > kFirstEchoTimer + 10000) nextEcho_ = kFirstEchoTimer;
        echoes_[id] = std::move(d);
        SetTimer(hwnd_, id, static_cast<UINT>(echoes_[id].delayMs), nullptr);
    };

    // Router -> engine / actions / timers / input thread.
    RouterSinks k;
    k.play = [this](HapticEvent e, std::optional<int> ordinal, int boost) {
        engine_.play(e, settings_.rule(e), engineParams(), now(), ordinal, boost);
    };
    k.runAction = [](const ButtonAction& a, bool) {
        win::debugLog("run action " + std::string(rawName(a.kind)));  // kind only: a shell command may hold private text
        win::runAction(a);
    };
    k.armTimer = [this](int id, int ms) { SetTimer(hwnd_, static_cast<UINT_PTR>(id), static_cast<UINT>(ms), nullptr); };
    k.cancelTimer = [this](int id) { KillTimer(hwnd_, static_cast<UINT_PTR>(id)); };
    k.gesturesBegin = [this] { input_.gesturesBegin(); win::debugLog("gesture mode begin"); };
    k.gesturesEnd = [this] { input_.gesturesEnd(); win::debugLog("gesture mode end"); };
    k.ringOpen = [this](bool hold) { ring_.open(settings_.effRingItems(), hold, settings_.accentHex); };
    k.ringToggle = [this] { ring_.toggle(settings_.effRingItems(), false, settings_.accentHex); };
    k.ringIsOpen = [this] { return ring_.isOpen(); };
    k.ringHoldEnded = [this] { ring_.holdEnded(); };
    k.systemVolume = [] { return win::systemVolume(); };
    router_ = std::make_unique<Router>(settings_, std::move(k));

    // Action Ring window and its pulses; a commit runs the action once the ring is gone (80 ms).
    ring_.create(inst_);
    ring_.onDetent = [this] { router_->systemEvent(HapticEvent::ringDetent, now()); };
    ring_.onOpen = [this] { router_->systemEvent(HapticEvent::ringOpen, now()); };
    ring_.onCommit = [this](const RingItem& item) {
        router_->systemEvent(HapticEvent::ringCommit, now());
        pendingRingAction_ = item.action;
        SetTimer(hwnd_, kTimerRingCommit, 80, nullptr);
    };
    ring_.onStateChanged = [this](bool open, bool hold) { input_.setRingState(open, hold); };

    // Device thread -> main.
    hid_.onChange = [this](const win::DeviceInfo&) { PostMessageW(hwnd_, WM_APP_HID, 0, 0); };
    hid_.start();

    // Input thread.
    if (!input_.start(hwnd_, WM_APP_INPUT)) win::debugLog("input thread failed to start");
    win::debugLog(std::string("hooks installed=") + (input_.hooksInstalled() ? "yes" : "no"));

    // Foreground app -> profiles and the appSwitch pulse.
    foreground_.start([this](std::string name, DWORD) { router_->foregroundChanged(std::move(name), now()); });
    if (HWND fg = GetForegroundWindow()) {
        std::string name = win::ForegroundWatch::imageNameOf(fg);
        if (!name.empty()) {
            for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            settings_.frontBundleID = name;
        }
    }

    // Plugin bridge.
    bridge_.waveformsJson = [this] {
        std::string out = "[";
        for (const auto& w : device_.table) {
            std::string n(w.name());
            for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (out.size() > 1) out += ",";
            out += "{\"name\":\"" + n + "\",\"ordinal\":" + std::to_string(w.ordinal) + ",\"ms\":" + std::to_string(w.durationMs) + "}";
        }
        return out + "]";
    };
    bridge_.onPlay = [this](std::string name, int intensity) {
        auto* p = new BridgePlay{std::move(name), intensity};
        if (!PostMessageW(hwnd_, WM_APP_BRIDGE, 0, reinterpret_cast<LPARAM>(p))) delete p;
    };
    startBridgeIfWanted();

    tray_.add(hwnd_, WM_APP_TRAY, icon_, L"iXpat");
    updateTray();
    pushConfig();
    SetTimer(hwnd_, kTimerFlush, 100, nullptr);
    SetTimer(hwnd_, kTimerWatchdog, 5000, nullptr);
    onDeviceChanged();
}

void App::shutdown() {
    KillTimer(hwnd_, kTimerFlush);
    KillTimer(hwnd_, kTimerWatchdog);
    if (store_) {
        store_->stopWatching();
        store_->flush();
    }
    if (settings_.managePointer) pointer_.restore();
    input_.gesturesEnd();  // never leave the pointer frozen
    ring_.destroy();
    input_.stop();
    foreground_.stop();
    bridge_.stop();
    hid_.stop();
    tray_.remove();
    win::debugLog("quit");
}

void App::startBridgeIfWanted() {
    if (settings_.bridgeEnabled) {
        if (bridge_.running() && (bridgePort_ != settings_.bridgePort || bridgeToken_ != settings_.bridgeToken)) bridge_.stop();
        if (!bridge_.running()) {
            bridgePort_ = settings_.bridgePort;
            bridgeToken_ = settings_.bridgeToken;
            bridge_.start(static_cast<std::uint16_t>(settings_.bridgePort), settings_.bridgeToken);
        }
    } else if (bridge_.running()) {
        bridge_.stop();
    }
}

/// Snapshot the settings the input thread needs (the Mac's pushConfig).
void App::pushConfig() {
    const bool recent = now() - lastDetach_ < 1800;
    const bool live = settings_.enabled && (device_.connected || recent || anyMouse_);
    win::InputConfig c;
    c.enabled = settings_.enabled;
    c.kinetic.enabled = live && settings_.effSmoothScroll();
    c.kinetic.pxPerNotch = settings_.scrollPixelsPerNotch;
    c.kinetic.glide = settings_.scrollGlide;
    c.kinetic.momentum = settings_.scrollMomentum;
    c.kinetic.accel = settings_.scrollAccel;
    c.kinetic.multiplier = router_ && router_->freeSpinActive() ? settings_.freeSpinMultiplier : 1;
    c.kinetic.reverse = live && settings_.reverseScroll;
    c.reverseOnly = live && !settings_.effSmoothScroll() && settings_.reverseScroll;
    c.shapeWheel = live;
    c.swallowChords = settings_.enabled && settings_.swallowButton;
    c.holdActionEnabled = settings_.enabled && settings_.forceClick;
    c.holdActionMs = settings_.forceClickMs;
    c.holdAction = settings_.windows.holdAction;
    c.gesture.up = settings_.effGestureUp();
    c.gesture.down = settings_.effGestureDown();
    c.gesture.left = settings_.effGestureLeft();
    c.gesture.right = settings_.effGestureRight();
    c.gesture.stepPx = settings_.gestureStepPx;
    c.gesture.swipePx = settings_.gestureSwipePx;
    c.gesture.freezePointer = settings_.gestureFreezePointer;
    c.anyMouse = anyMouse_;
    input_.setConfig(c);
    lastFreeSpin_ = router_ && router_->freeSpinActive();
}

void App::onInputEvents() {
    win::InputEvent e;
    while (input_.pop(e)) {
        switch (e.kind) {
            case win::InputEvent::Kind::rawButtons: {
                const auto oldMask = static_cast<std::uint8_t>(e.a), newMask = static_cast<std::uint8_t>(e.b);
                if ((newMask & ~oldMask) & 0x01) {
                    if (auto r = win::foregroundFrame()) frameBeforeDrag_ = IRect{r->left, r->top, r->right, r->bottom};
                }
                router_->buttons(oldMask, newMask, e.now);
                char buf[48];
                std::snprintf(buf, sizeof buf, "mouse buttons %02x -> %02x", oldMask, newMask);
                win::reportLog(buf);
                break;
            }
            case win::InputEvent::Kind::rawWheel:
                router_->wheel(e.b, e.now);
                if (router_->freeSpinActive() != lastFreeSpin_) pushConfig();
                break;
            case win::InputEvent::Kind::rawKey: {
                char buf[48];
                std::snprintf(buf, sizeof buf, "keyboard vk %02x %s", e.b, e.c ? "down" : "up");
                win::reportLog(buf);
                break;
            }
            case win::InputEvent::Kind::chord: {
                const auto out = static_cast<ChordOutput>(e.b);
                win::debugLog(std::string("action button: ") + (out == ChordOutput::press ? "press" : out == ChordOutput::doublePress ? "doublePress" : out == ChordOutput::holdBegin ? "hold" : "holdEnd"));
                router_->chord(out, e.now);
                break;
            }
            case win::InputEvent::Kind::systemEvent:
                router_->systemEvent(static_cast<HapticEvent>(e.b), e.now);
                break;
            case win::InputEvent::Kind::gestureAction: {
                const ButtonAction& a = e.b == 0 ? settings_.effGestureUp() : e.b == 1 ? settings_.effGestureDown() : e.b == 2 ? settings_.effGestureLeft() : settings_.effGestureRight();
                router_->gestureFired(a, e.now);
                break;
            }
            case win::InputEvent::Kind::dragEnded:
                SetTimer(hwnd_, kTimerSnapCheck, 400, nullptr);
                break;
            case win::InputEvent::Kind::ringClick:
                ring_.close(true);
                break;
            case win::InputEvent::Kind::ringCancel:
                ring_.close(false);
                break;
            case win::InputEvent::Kind::ringOpenRequest:
                ring_.toggle(settings_.effRingItems(), false, settings_.accentHex);
                break;
            case win::InputEvent::Kind::hookLost:
                input_.reinstallHooks();
                break;
        }
    }
}

void App::snapCheck() {
    auto before = frameBeforeDrag_;
    frameBeforeDrag_.reset();
    auto r = win::foregroundFrame();
    if (!before || !r) return;
    const IRect nowRect{r->left, r->top, r->right, r->bottom};
    auto work = win::workAreaFor(*r);
    if (!work) return;
    if (looksSnapped(*before, nowRect, IRect{work->left, work->top, work->right, work->bottom})) {
        router_->systemEvent(HapticEvent::windowSnap, now());
    }
}

void App::onDeviceChanged() {
    const bool wasConnected = device_.connected;
    device_ = hid_.snapshot();
    engine_.setConnected(device_.connected);
    engine_.setTable(device_.table);
    router_->setTable(device_.table);
    if (wasConnected && !device_.connected) {
        lastDetach_ = now();
        router_->deviceConnected(false, now());
    }
    if (!wasConnected && device_.connected) {
        router_->deviceConnected(true, now());
        if (settings_.managePointer) SetTimer(hwnd_, kTimerPointerApply, 1000, nullptr);
    }
    updateTray();
    pushConfig();
}

void App::applyPointer() {
    if (settings_.managePointer && (device_.connected || anyMouse_)) {
        pointer_.apply(settings_.pointerSpeed, settings_.noAcceleration, settings_.scrollSpeed);
    } else {
        pointer_.restore();
    }
}

void App::reloadSettings() {
    Settings fresh = store_->load();
    fresh.frontBundleID = settings_.frontBundleID;
    settings_ = fresh;
    win::debugLog("settings reloaded from disk");
    settingsChanged();
}

void App::settingsChanged() {
    pushConfig();
    startBridgeIfWanted();
    applyPointer();
    updateTray();
}

void App::updateTray() {
    std::wstring tip = L"iXpat " IXPAT_VERSION_WSTRING;
    tip += device_.connected ? L"\nSurface Mouse connected" : L"\nSurface Mouse not connected";
    if (!settings_.enabled) tip += L" (disabled)";
    tray_.setTip(tip);
}

void App::showMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (settings_.enabled ? MF_CHECKED : 0), kMenuEnabled, L"Enabled");
    AppendMenuW(menu, MF_STRING | (settings_.hapticsEnabled ? MF_CHECKED : 0), kMenuHaptics, L"Haptics");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuOpenSettings, L"Open settings.json");
    AppendMenuW(menu, MF_STRING, kMenuOpenLogs, L"Open logs folder");
    AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"About iXpat");
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // so the menu closes when the user clicks elsewhere
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (cmd) {
        case kMenuEnabled:
            settings_.enabled = !settings_.enabled;
            store_->markDirty(settings_, now());
            settingsChanged();
            break;
        case kMenuHaptics:
            settings_.hapticsEnabled = !settings_.hapticsEnabled;
            store_->markDirty(settings_, now());
            settingsChanged();
            break;
        case kMenuOpenSettings:
            if (!std::filesystem::exists(win::settingsPath())) store_->save(settings_);
            ShellExecuteW(nullptr, L"open", win::settingsPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kMenuOpenLogs:
            ShellExecuteW(nullptr, L"open", win::appDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kMenuReload:
            reloadSettings();
            break;
        case kMenuAbout:
            showAbout();
            break;
        case kMenuQuit:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
    }
}

void App::showAbout() {
    std::wstring text = L"iXpat " IXPAT_VERSION_WSTRING L" for Windows\n\n";
    text += device_.connected ? L"Surface Mouse (2nd Edition): connected\n" : L"Surface Mouse (2nd Edition): not connected\n";
    text += L"Firmware: " + std::wstring(device_.firmware.begin(), device_.firmware.end()) + L"\n";
    text += L"Battery: " + (device_.battery ? std::to_wstring(*device_.battery) + L"%" : std::wstring(L"unknown")) + L"\n";
    text += L"Waveform table: " + std::wstring(device_.tableFromDevice ? L"from the device" : L"measured defaults (fw 5.29)") + L"\n";
    text += L"Hooks: " + std::wstring(input_.hooksInstalled() ? L"installed" : L"NOT installed") + L"\n";
    text += L"Bridge: " + (bridge_.running() ? L"127.0.0.1:" + std::to_wstring(bridge_.port()) : std::wstring(L"off")) + L"\n";
    if (!device_.lastError.empty()) text += L"Last error: " + std::wstring(device_.lastError.begin(), device_.lastError.end()) + L"\n";
    text += L"\nSettings and logs: " + win::appDataDir().wstring() + L"\n\nMIT License. Not affiliated with Microsoft.";
    MessageBoxW(nullptr, text.c_str(), L"About iXpat", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_INPUT:
            onInputEvents();
            return 0;
        case WM_APP_HID:
            onDeviceChanged();
            return 0;
        case WM_APP_SETTINGS:
            reloadSettings();
            return 0;
        case WM_APP_BRIDGE: {
            std::unique_ptr<BridgePlay> p(reinterpret_cast<BridgePlay*>(lp));
            if (!p) return 0;
            // Plugins: gated by the master switches and the pluginPulse rule, scaled by the master strength.
            if (!settings_.enabled || !settings_.effHapticsEnabled() || !settings_.rule(HapticEvent::pluginPulse).enabled ||
                settings_.masterIntensity <= 0) return 0;
            auto usage = usageForLowercaseName(p->name);
            if (!usage) return 0;
            auto ordinal = ordinalFor(*usage, device_.table);
            if (!ordinal) return 0;
            const int scaled = std::max(1, static_cast<int>(std::lround(p->intensity * settings_.masterIntensity / 4.0)));
            engine_.play(*ordinal, scaled, "plugin " + p->name, now());
            return 0;
        }
        case WM_APP_SHOW:
            showAbout();
            return 0;
        case WM_APP_TRAY: {
            const UINT event = LOWORD(lp);
            if (event == WM_LBUTTONUP || event == WM_RBUTTONUP || event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) showMenu();
            return 0;
        }
        case WM_TIMER: {
            const UINT_PTR id = wp;
            if (id == kTimerFlush) {
                store_->flushIfDue(now());
            } else if (id == kTimerWatchdog) {
                const std::uint32_t hb = input_.heartbeat(), raw = input_.rawCount();
                if (raw != lastRaw_ && hb == lastHeartbeat_ && lastRaw_ != 0) {
                    win::debugLog("watchdog: raw input flowing but no hook callbacks; re-installing hooks");
                    input_.reinstallHooks();
                }
                lastHeartbeat_ = hb;
                lastRaw_ = raw;
            } else if (id == kTimerSnapCheck) {
                KillTimer(hwnd_, kTimerSnapCheck);
                snapCheck();
            } else if (id == kTimerPointerApply) {
                KillTimer(hwnd_, kTimerPointerApply);
                applyPointer();
            } else if (id == kTimerRingCommit) {
                KillTimer(hwnd_, kTimerRingCommit);
                if (pendingRingAction_) {
                    ButtonAction a = *pendingRingAction_;
                    pendingRingAction_.reset();
                    router_->run(a, false);
                }
            } else if (id == kTimerDemoClose) {
                KillTimer(hwnd_, kTimerDemoClose);
                ring_.close(false);
                DestroyWindow(hwnd_);
            } else if (id >= kFirstEchoTimer) {
                KillTimer(hwnd_, id);
                auto it = echoes_.find(id);
                if (it != echoes_.end()) {
                    engine_.play(it->second.play.ordinal, it->second.play.intensity, it->second.label, now());
                    echoes_.erase(it);
                }
            } else {
                KillTimer(hwnd_, id);  // the router's one-shot hold / free-spin timers
                router_->timer(static_cast<int>(id), now());
                if (router_->freeSpinActive() != lastFreeSpin_) pushConfig();
            }
            return 0;
        }
        case WM_DISPLAYCHANGE:
            return 0;
        case WM_ENDSESSION:
            if (wp) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == win::Tray::taskbarCreatedMessage()) {
                tray_.onTaskbarCreated();
                return 0;
            }
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    App app;
    const bool demo = cmdLine && wcsstr(cmdLine, L"--ring-demo") != nullptr;
    return app.run(inst, demo);
}
