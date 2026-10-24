// iXpat for Windows: the wiring (the Mac AppDelegate). Everything else is a unit with one job.
#include <windows.h>
#include <objbase.h>
#include <commdlg.h>
#include <shellapi.h>
#include <commctrl.h>

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
#include "win/stall_watch.h"
#include "win/tray.h"
#include "win/uia_watch.h"
#include "core/version.h"
#include "win/login_item.h"
#include "win/updater.h"
#include "ui/ring_window.h"
#include "ui/settings_window.h"

#define IDI_IXPAT 101

namespace {

using namespace ixpat;

constexpr UINT WM_APP_INPUT = WM_APP + 1;
constexpr UINT WM_APP_HID = WM_APP + 2;
constexpr UINT WM_APP_SETTINGS = WM_APP + 3;
constexpr UINT WM_APP_BRIDGE = WM_APP + 4;
constexpr UINT WM_APP_TRAY = WM_APP + 5;
constexpr UINT WM_APP_SHOW = WM_APP + 6;
constexpr UINT WM_APP_UIA = WM_APP + 7;
constexpr UINT WM_APP_UPDATE = WM_APP + 8;
constexpr UINT WM_APP_STALL = WM_APP + 9;

constexpr UINT_PTR kTimerFlush = 4, kTimerWatchdog = 5, kTimerSnapCheck = 7, kTimerPointerApply = 8, kTimerRingCommit = 9,
                   kTimerDemoClose = 10, kTimerDetachExpiry = 11, kTimerPointerOwner = 12,
                   kTimerCursor = 13, kTimerUpdateFirst = 14, kTimerUpdate = 15;
constexpr UINT_PTR kFirstEchoTimer = 100;

enum MenuId : UINT { kMenuEnabled = 1, kMenuHaptics, kMenuSettings, kMenuOpenSettings, kMenuOpenLogs, kMenuReload, kMenuCheckUpdates, kMenuAbout, kMenuQuit };

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), wchar_t{});
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

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
    win::UiaWatch uia_;
    win::StallWatch stalls_;
    win::Updater updater_;
    win::UpdateState updateState_;
    void scheduledUpdateCheck();
    void onUpdateResult(bool manual, win::UpdateResult& r);
    void offerUpdate(const ReleaseInfo& r, bool manual);
    void applyLoginItem();
    HCURSOR arrowCursor_ = nullptr, waitCursor_ = nullptr, appStartingCursor_ = nullptr, lastCursor_ = nullptr;
    Seconds lastCursorTick_ = 0;
    void cursorTick();
    ui::RingWindow ring_;
    std::optional<ButtonAction> pendingRingAction_;
    bool ringDemo_ = false;
    ui::SettingsModel model_;
    std::unique_ptr<ui::SettingsWindow> window_;
    void syncModel();
    void showSettings();
    std::optional<std::string> pickFile(const wchar_t* filter);

    win::DeviceInfo device_;
    Seconds lastDetach_ = -1e9;
    bool anyMouse_ = false;
    bool lastFreeSpin_ = false;
    std::map<UINT_PTR, HapticEngine::Deferred> echoes_;
    UINT_PTR nextEcho_ = kFirstEchoTimer;
    std::optional<IRect> frameBeforeDrag_;
    std::uint32_t lastMouseCalls_ = 0, lastKeyCalls_ = 0, lastRawMouse_ = 0, lastRawKey_ = 0;
    std::string bridgeToken_;
    int bridgePort_ = 0;
    // Read by the bridge thread: a snapshot replaced under the lock whenever the table changes.
    mutable SRWLOCK waveformsLock_ = SRWLOCK_INIT;
    std::string waveformsJson_ = "[]";
    bool pointerOwnerSurface_ = false;
    bool quitting_ = false;
    unsigned flushTicks_ = 0;
    int modalDepth_ = 0;
    bool reloadPending_ = false;
    void quit();
    void refreshWaveformsJson();
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
    quit();
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
    engine_.trigger = [this](HapticEngine::Play p, std::string_view) {
        hid_.trigger(p.ordinal, p.intensity);
        if (window_ && window_->isVisible()) window_->pulse();
    };
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
    ring_.onLog = [](const std::string& s) { win::debugLog(s); };
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
    uia_.now = [this] { return now(); };
    uia_.lastPointerMove = [this] { return input_.lastPointerMove(); };
    uia_.leftButtonDown = [this] { return input_.leftButtonDown(); };
    uia_.start(hwnd_, WM_APP_UIA);
    stalls_.start(hwnd_, WM_APP_STALL);
    arrowCursor_ = LoadCursorW(nullptr, IDC_ARROW);
    waitCursor_ = LoadCursorW(nullptr, IDC_WAIT);
    appStartingCursor_ = LoadCursorW(nullptr, IDC_APPSTARTING);
    SetTimer(hwnd_, kTimerCursor, 50, nullptr);
    updater_.init(hwnd_, WM_APP_UPDATE, L"iXpat-Windows/" IXPAT_VERSION_WSTRING);
    updateState_ = win::loadUpdateState();
    SetTimer(hwnd_, kTimerUpdateFirst, 10 * 1000, nullptr);   // Updater.start: 10 s after launch, then every 6 h
    SetTimer(hwnd_, kTimerUpdate, 6 * 3600 * 1000, nullptr);
    // The Run key is the truth at launch (the installer's "start at login" task, Task Manager); edits write it back.
    if (!anyMouse_) settings_.windows.startAtLogin = win::loginItemEnabled();
    {
        const auto st = input_.status();
        win::debugLog(std::string("raw input registered=") + (st.rawRegistered ? "yes" : "no") + " hooks installed=" +
                      ((st.mouseHook && st.keyboardHook) ? "yes" : "NO (haptics-only mode: nothing is shaped or hidden)"));
    }

    // Foreground app -> profiles and the appSwitch pulse.
    foreground_.start([this](std::string name, DWORD) {
        router_->foregroundChanged(std::move(name), now());
        pushConfig();  // the front app's profile may change the gestures and the wheel
    });
    if (HWND fg = GetForegroundWindow()) {
        std::string name = win::ForegroundWatch::imageNameOf(fg);
        if (!name.empty()) {
            for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            settings_.frontBundleID = name;
        }
    }

    // Plugin bridge (its thread reads only the JSON snapshot, never the table).
    refreshWaveformsJson();
    bridge_.waveformsJson = [this] {
        AcquireSRWLockShared(&waveformsLock_);
        std::string copy = waveformsJson_;
        ReleaseSRWLockShared(&waveformsLock_);
        return copy;
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

    // The settings window (MainWindow.swift). Opens on every launch unless IXPAT_NO_WINDOW=1, like the Mac.
    ui::SettingsActions acts;
    acts.changed = [this] {
        store_->markDirty(settings_, now());
        settingsChanged();
    };
    acts.preview = [this](const HapticRule& r, const std::string& label) { engine_.preview(r, label, now()); syncModel(); };
    acts.run = [this](const ButtonAction& a, bool hold) { router_->run(a, hold); };
    acts.openRing = [this] { ring_.toggle(settings_.effRingItems(), false, settings_.accentHex); };
    acts.testPulse = [this] {
        engine_.play(ordinalFor(0x1015, device_.table).value_or(7), 4, "Test", now());
        syncModel();
    };
    acts.newToken = [this] {
        settings_.bridgeToken = newBridgeToken();
        store_->markDirty(settings_, now());
        startBridgeIfWanted();
    };
    acts.readTable = [this] { hid_.refreshTable(); };
    acts.copyText = [this](const std::string& s) {
        std::wstring w(s.begin(), s.end());
        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (h) {
            if (void* p = GlobalLock(h)) {
                memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        CloseClipboard();
    };
    acts.showSetup = [this] { showAbout(); };
    acts.showAbout = [this] { showAbout(); };
    acts.checkUpdates = [this] {
        if (updater_.check(true)) {
            model_.updateChecking = true;
            model_.updateStatus = "Checking GitHub...";
            syncModel();
        }
    };
    acts.openUrl = [](const std::string& url) {
        static const char* const kAllowed[] = {"https://github.com/knightynite/ixpat", "https://knightynite.github.io/ixpat/",
                                               "https://www.linkedin.com/in/alnnajafi/"};
        for (const char* prefix : kAllowed) {
            if (url.rfind(prefix, 0) == 0) {
                ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return;
            }
        }
    };
    acts.openMouseSettings = [] { ShellExecuteW(nullptr, L"open", L"ms-settings:mousetouchpad", nullptr, nullptr, SW_SHOWNORMAL); };
    acts.runningApps = [] {
        std::vector<std::pair<std::string, std::string>> apps;
        EnumWindows(
            [](HWND h, LPARAM p) -> BOOL {
                auto* out = reinterpret_cast<std::vector<std::pair<std::string, std::string>>*>(p);
                if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
                wchar_t title[256]{};
                if (!GetWindowTextW(h, title, 256) || !title[0]) return TRUE;
                std::string image = win::ForegroundWatch::imageNameOf(h);
                if (image.empty() || image == "iXpat.exe") return TRUE;
                for (auto& a : *out) if (a.first == image) return TRUE;
                int n = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
                std::string t(static_cast<std::size_t>(std::max(0, n - 1)), '\0');
                if (n > 1) WideCharToMultiByte(CP_UTF8, 0, title, -1, t.data(), n, nullptr, nullptr);
                out->emplace_back(image, t);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&apps));
        std::sort(apps.begin(), apps.end(), [](auto& a, auto& b) { return a.second < b.second; });
        return apps;
    };
    acts.chooseExe = [this] { return pickFile(L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0"); };
    acts.chooseApp = [this] { return pickFile(L"Programs and shortcuts (*.exe;*.lnk)\0*.exe;*.lnk\0All files (*.*)\0*.*\0"); };
    window_ = std::make_unique<ui::SettingsWindow>(settings_, model_, std::move(acts));
    window_->create(inst_, icon_);
    syncModel();
    char noWin[8];
    const bool suppress = GetEnvironmentVariableA("IXPAT_NO_WINDOW", noWin, sizeof noWin) > 0 && noWin[0] == '1';
    if (!suppress && !ringDemo_) showSettings();
}

void App::refreshWaveformsJson() {
    std::string out = "[";
    for (const auto& w : device_.table) {
        std::string n(w.name());
        for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (out.size() > 1) out += ",";
        out += "{\"name\":\"" + n + "\",\"ordinal\":" + std::to_string(w.ordinal) + ",\"ms\":" + std::to_string(w.durationMs) + "}";
    }
    out += "]";
    AcquireSRWLockExclusive(&waveformsLock_);
    waveformsJson_ = std::move(out);
    ReleaseSRWLockExclusive(&waveformsLock_);
}

std::optional<std::string> App::pickFile(const wchar_t* filter) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = window_ ? window_->hwnd() : hwnd_;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ++modalDepth_;
    const BOOL ok = GetOpenFileNameW(&ofn);
    --modalDepth_;
    if (reloadPending_ && modalDepth_ == 0) PostMessageW(hwnd_, WM_APP_SETTINGS, 0, 0);  // after the caller is done with its references
    if (!ok) return std::nullopt;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, path, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void App::showSettings() {
    if (!window_) return;
    syncModel();
    window_->applyTheme();
    window_->show();
}

/// Copy the live state the window shows (device, engine, input status) and repaint it.
void App::syncModel() {
    model_.device = device_;
    model_.pulseCount = engine_.pulseCount();
    model_.lastEvent = engine_.lastEvent();
    model_.lastPlayed = engine_.lastPlayed();
    model_.input = input_.status();
    model_.uia = uia_.stats();
    model_.stallMaxMs = stalls_.maxStallMs();
    model_.stallCount = stalls_.count();
    model_.bridgeRunning = bridge_.running();
    model_.frontApp = settings_.frontBundleID.value_or("");
    if (window_) window_->refresh();
}

/// Stop every producer while the window still exists, then let the window go.
void App::quit() {
    if (quitting_) return;
    quitting_ = true;
    window_.reset();
    KillTimer(hwnd_, kTimerFlush);
    KillTimer(hwnd_, kTimerWatchdog);
    KillTimer(hwnd_, kTimerCursor);
    KillTimer(hwnd_, kTimerUpdateFirst);
    KillTimer(hwnd_, kTimerUpdate);
    if (store_) {
        store_->stopWatching();
        store_->flush();
    }
    pointer_.restore();
    input_.gesturesEnd();  // never leave the pointer frozen
    ring_.destroy();
    input_.stop();
    uia_.stop();
    stalls_.stop();
    updater_.stop();
    foreground_.stop();
    bridge_.stop();
    hid_.stop();
    tray_.remove();
    // Payloads posted by the bridge thread before it stopped are ours to free.
    MSG m;
    while (PeekMessageW(&m, hwnd_, WM_APP_BRIDGE, WM_APP_BRIDGE, PM_REMOVE)) delete reinterpret_cast<BridgePlay*>(m.lParam);
    while (PeekMessageW(&m, hwnd_, WM_APP_UPDATE, WM_APP_UPDATE, PM_REMOVE)) delete reinterpret_cast<win::UpdateResult*>(m.lParam);
    win::debugLog("quit");
    DestroyWindow(hwnd_);
}

void App::shutdown() { quit(); }

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
    c.gesture.up = settings_.effGestureUp().kind;
    c.gesture.down = settings_.effGestureDown().kind;
    c.gesture.left = settings_.effGestureLeft().kind;
    c.gesture.right = settings_.effGestureRight().kind;
    c.gesture.stepPx = settings_.gestureStepPx;
    c.gesture.swipePx = settings_.gestureSwipePx;
    c.gesture.freezePointer = settings_.gestureFreezePointer;
    c.anyMouse = anyMouse_;
    input_.setConfig(c);
    lastFreeSpin_ = router_ && router_->freeSpinActive();
    // Accessibility ticks poll only while a rule wants them and the mouse can play them.
    const bool ticks = live && settings_.effHapticsEnabled();
    win::UiaConfig u;
    u.menuTicks = ticks && settings_.rule(HapticEvent::menuHighlight).enabled;
    u.textTicks = ticks && (settings_.rule(HapticEvent::textSelect).enabled || settings_.rule(HapticEvent::textLine).enabled);
    u.controlTicks = ticks && settings_.rule(HapticEvent::controlHover).enabled;
    u.dropTicks = ticks && settings_.rule(HapticEvent::dropTarget).enabled;
    u.sliderTicks = ticks && settings_.rule(HapticEvent::sliderTick).enabled;
    u.selectionTicks = ticks && settings_.rule(HapticEvent::selectionChange).enabled;
    u.scrollEndTicks = ticks && settings_.rule(HapticEvent::scrollEnd).enabled;
    uia_.setConfig(u);
}

/// Hover affordance from the cursor shape (HoverWatcher.swift): arrow -> hand / I-beam / resize ticks once
/// on arrival, leaving is silent; under a drag any change (the drop badge) ticks. Busy cursors and our own
/// windows never tick.
void App::cursorTick() {
    const bool hover = settings_.rule(HapticEvent::hoverAffordance).enabled;
    const bool drop = settings_.rule(HapticEvent::dropTarget).enabled;
    const bool live = settings_.enabled && settings_.effHapticsEnabled() && (device_.connected || now() - lastDetach_ < 1800 || anyMouse_);
    if (!live || !(hover || drop)) return;
    const Seconds t = now();
    if (t - input_.lastPointerMove() > 0.5) return;
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) return;
    if (ci.hCursor == lastCursor_) return;
    const HCURSOR prev = lastCursor_;
    lastCursor_ = ci.hCursor;
    if (prev == nullptr) return;  // the first sample is the baseline
    if (t - lastCursorTick_ < 0.08) return;
    if (ci.hCursor == waitCursor_ || ci.hCursor == appStartingCursor_) return;
    HWND under = WindowFromPoint(ci.ptScreenPos);
    DWORD pid = 0;
    if (under) GetWindowThreadProcessId(under, &pid);
    if (pid == GetCurrentProcessId()) return;
    if (input_.leftButtonDown()) {
        if (!drop) return;
        lastCursorTick_ = t;
        router_->systemEvent(HapticEvent::dropTarget, t);
    } else {
        if (!hover || ci.hCursor == arrowCursor_) return;
        lastCursorTick_ = t;
        router_->systemEvent(HapticEvent::hoverAffordance, t);
    }
}

void App::onInputEvents() {
    win::InputEvent e;
    bool any = false;
    while (input_.pop(e)) {
        any = true;
        switch (e.kind) {
            case win::InputEvent::Kind::rawButtons: {
                const auto oldMask = static_cast<std::uint8_t>(e.a), newMask = static_cast<std::uint8_t>(e.b);
                model_.buttons = newMask;
                if ((newMask & ~oldMask) & 0x01) {
                    if (auto r = win::foregroundFrame()) frameBeforeDrag_ = IRect{r->left, r->top, r->right, r->bottom};
                }
                router_->buttons(oldMask, newMask, e.now);
                if ((oldMask & ~newMask) & 0x01) uia_.noteClick();  // a release: did a tab / row become selected?
                char buf[48];
                std::snprintf(buf, sizeof buf, "mouse buttons %02x -> %02x", oldMask, newMask);
                win::reportLog(buf);
                model_.reportLog.push_back(buf);
                if (model_.reportLog.size() > 150) model_.reportLog.pop_front();
                break;
            }
            case win::InputEvent::Kind::rawWheel:
                model_.wheelTotal += e.b;
                router_->wheel(e.b, e.now);
                uia_.noteWheel();
                if (router_->freeSpinActive() != lastFreeSpin_) pushConfig();
                break;
            case win::InputEvent::Kind::rawKey: {
                char buf[64];
                const bool down = (e.c & 1) != 0;
                if (e.a >= 2) {
                    std::snprintf(buf, sizeof buf, "hook vk %02x %s %s", e.b, down ? "down" : "up", e.a == 3 ? "swallowed" : "passed");
                } else {
                    std::snprintf(buf, sizeof buf, "keyboard vk %02x %s%s%s", e.b, down ? "down" : "up", e.a == 1 ? " (other device)" : "",
                                  (e.c & 2) ? " (no hook callback)" : "");
                }
                win::reportLog(buf);
                model_.reportLog.push_back(buf);
                if (model_.reportLog.size() > 150) model_.reportLog.pop_front();
                break;
            }
            case win::InputEvent::Kind::chord: {
                const auto out = static_cast<ChordOutput>(e.b);
                const char* name = out == ChordOutput::press ? "Press" : out == ChordOutput::doublePress ? "Double press" : out == ChordOutput::holdBegin ? "Press and hold" : "Hold released";
                win::debugLog(std::string("action button: ") + name);
                if (out != ChordOutput::holdEnd) model_.lastGesture = name;
                model_.actionPressed = out == ChordOutput::holdBegin;
                router_->chord(out, e.now);
                break;
            }
            case win::InputEvent::Kind::systemEvent:
                router_->systemEvent(static_cast<HapticEvent>(e.b), e.now);
                break;
            case win::InputEvent::Kind::gestureAction: {
                if (!router_->gestureModeActive()) break;  // a move that slipped in after the hold ended
                const ButtonAction& a = e.b == kGestureUp ? settings_.effGestureUp() : e.b == kGestureDown ? settings_.effGestureDown() : e.b == kGestureLeft ? settings_.effGestureLeft() : settings_.effGestureRight();
                router_->gestureFired(a, e.now);
                break;
            }
            case win::InputEvent::Kind::pointerOwner:
                pointerOwnerSurface_ = e.b != 0;
                SetTimer(hwnd_, kTimerPointerOwner, 300, nullptr);  // debounced apply / restore
                break;
            case win::InputEvent::Kind::overflow:
                win::debugLog("input ring buffer overflowed; re-synchronising gesture and hold state");
                router_->reset();
                input_.gesturesEnd();
                break;
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
            case win::InputEvent::Kind::hooksReinstalled: {
                const auto st = input_.status();
                std::string why = e.b == 0 ? "asked by the watchdog" : std::string(e.b & 1 ? "mouse" : "") + ((e.b & 3) == 3 ? " and " : "") +
                                                                       (e.b & 2 ? "keyboard" : "") + " reports went unanswered";
                win::debugLog("hooks re-installed (" + why + "; " + std::to_string(st.hookReinstalls) + " so far; slowest callback " +
                              std::to_string(st.maxHookMicros) + " us, slowest chain " + std::to_string(st.maxChainMicros) + " us, longest lag " +
                              std::to_string(st.maxHookLagMs) + " ms" + (e.c ? ", SetWindowsHookEx error " + std::to_string(e.c) : std::string()) + ")");
                break;
            }

        }
    }
    if (any && window_ && window_->isVisible()) syncModel();
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
        SetTimer(hwnd_, kTimerDetachExpiry, 1800 * 1000, nullptr);  // the recently-detached allowance ends then
    }
    refreshWaveformsJson();
    if (!wasConnected && device_.connected) {
        router_->deviceConnected(true, now());
        if (settings_.managePointer) SetTimer(hwnd_, kTimerPointerApply, 1000, nullptr);
    }
    updateTray();
    pushConfig();
    syncModel();
}

/// The Surface Mouse's pointer settings apply while it is the device moving the pointer; another
/// device moving it restores the system values (debounced 300 ms by the pointerOwner timer).
void App::applyPointer() {
    const bool ours = settings_.managePointer && (device_.connected || anyMouse_) && (pointerOwnerSurface_ || anyMouse_);
    if (ours) pointer_.apply(settings_.pointerSpeed, settings_.noAcceleration, settings_.scrollSpeed);
    else pointer_.restore();
}

void App::reloadSettings() {
    reloadPending_ = false;
    if (window_) window_->cancelEditing();  // native edit / popups point into the model being replaced
    Settings fresh = store_->load();
    fresh.frontBundleID = settings_.frontBundleID;
    settings_ = fresh;
    win::debugLog("settings reloaded from disk");
    settingsChanged();
}

void App::settingsChanged() {
    if (!settings_.enabled) {
        router_->reset();  // end a hold / gesture mode / held ring at once
        input_.gesturesEnd();
    }
    pushConfig();
    startBridgeIfWanted();
    applyPointer();
    applyLoginItem();
    updateTray();
    if (window_) window_->applyTheme();
    syncModel();
}

void App::applyLoginItem() {
    if (anyMouse_) return;  // dev runs (IXPAT_ANY_MOUSE) never touch the Run key
    if (win::loginItemEnabled() != settings_.windows.startAtLogin && !win::setLoginItem(settings_.windows.startAtLogin))
        win::debugLog("login item: HKCU Run could not be updated");
}

/// Background checks hit the network at most once a day (Updater.check(manual: false)).
void App::scheduledUpdateCheck() {
    if (!settings_.autoCheckUpdates) return;
    if (win::unixNow() - updateState_.lastCheck < 24 * 3600) return;
    updater_.check(false);
}

void App::onUpdateResult(bool manual, win::UpdateResult& r) {
    model_.updateChecking = false;
    if (!r.ok) {
        win::debugLog("update check failed: " + r.error);
        model_.updateStatus = "Update check failed: " + r.error;
        syncModel();
        if (manual) MessageBoxW(hwnd_, (L"The update check failed.\n\n" + widen(r.error)).c_str(), L"iXpat", MB_OK | MB_ICONWARNING);
        return;
    }
    updateState_.lastCheck = win::unixNow();
    win::saveUpdateState(updateState_);
    if (!r.release) {
        win::debugLog("update check: no Windows release found, running " IXPAT_VERSION_STRING);
        model_.updateStatus = "No Windows release has been published yet.";
        model_.updateAvailable.reset();
        syncModel();
        if (manual) MessageBoxW(hwnd_, L"No Windows release has been published yet.\nYou are running a development build.", L"iXpat", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::string& latest = r.release->version;
    win::debugLog("update check: latest " + latest + ", running " IXPAT_VERSION_STRING);
    if (!isNewerVersion(latest, IXPAT_VERSION_STRING)) {
        model_.updateStatus = "Up to date: " IXPAT_VERSION_STRING " is the latest Windows release.";
        model_.updateAvailable.reset();
        syncModel();
        if (manual) MessageBoxW(hwnd_, L"iXpat " IXPAT_VERSION_WSTRING L" is the latest Windows release.", L"iXpat is up to date", MB_OK | MB_ICONINFORMATION);
        return;
    }
    model_.updateStatus = "iXpat " + latest + " is available.";
    model_.updateAvailable = *r.release;
    syncModel();
    if (!manual && updateState_.skipped == latest) return;
    offerUpdate(*r.release, manual);
}

/// The Mac's alert: Download (here: open the release page, the build is unsigned) / Later / Skip this version.
void App::offerUpdate(const ReleaseInfo& r, bool manual) {
    const std::wstring title = L"iXpat " + widen(r.version) + L" is available";
    const std::wstring body = L"You have version " IXPAT_VERSION_WSTRING L". The release page has the zip and the installer.";
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof cfg;
    cfg.hwndParent = window_ ? window_->hwnd() : hwnd_;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.pszWindowTitle = L"iXpat";
    cfg.pszMainInstruction = title.c_str();
    cfg.pszContent = body.c_str();
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    const TASKDIALOG_BUTTON buttons[] = {{100, L"Open release page"}, {101, L"Later"}, {102, L"Skip this version"}};
    cfg.pButtons = buttons;
    cfg.cButtons = 3;
    (void)manual;
    cfg.nDefaultButton = 100;
    int pressed = 101;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) {
        pressed = MessageBoxW(cfg.hwndParent, (title + L"\n\n" + body + L"\n\nOpen the release page?").c_str(), L"iXpat", MB_YESNO | MB_ICONINFORMATION) == IDYES ? 100 : 101;
    }
    if (pressed == 100 && isWindowsReleaseTag(r.tag)) ShellExecuteW(nullptr, L"open", widen(canonicalReleaseUrl(r.tag)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (pressed == 102) {
        updateState_.skipped = r.version;
        win::saveUpdateState(updateState_);
    }
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
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING, kMenuOpenSettings, L"Open settings.json");
    AppendMenuW(menu, MF_STRING, kMenuOpenLogs, L"Open logs folder");
    AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (updater_.busy() ? MF_GRAYED : 0), kMenuCheckUpdates, L"Check for updates...");
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
        case kMenuSettings:
            showSettings();
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
        case kMenuCheckUpdates:
            if (updater_.check(true)) {
                model_.updateChecking = true;
                model_.updateStatus = "Checking GitHub...";
                syncModel();
            }
            break;
        case kMenuAbout:
            showAbout();
            break;
        case kMenuQuit:
            quit();
            break;
        default:
            break;
    }
}

void App::showAbout() {
    if (window_) {  // the About pane; the message box below is the IXPAT_NO_WINDOW fallback
        showSettings();
        window_->showAbout();
        return;
    }
    std::wstring text = L"iXpat " IXPAT_VERSION_WSTRING L" for Windows\n\n";
    text += device_.connected ? L"Surface Mouse (2nd Edition): connected\n" : L"Surface Mouse (2nd Edition): not connected\n";
    text += L"Firmware: " + std::wstring(device_.firmware.begin(), device_.firmware.end()) + L"\n";
    text += L"Battery: " + (device_.battery ? std::to_wstring(*device_.battery) + L"%" : std::wstring(L"unknown")) + L"\n";
    text += L"Waveform table: " + std::wstring(device_.tableFromDevice ? L"from the device" : L"measured defaults (fw 5.29)") + L"\n";
    {
        const auto st = input_.status();
        text += L"Hooks: " + std::wstring((st.mouseHook && st.keyboardHook) ? L"installed" : L"NOT installed") + L", raw input " +
                (st.rawRegistered ? L"registered" : L"NOT registered") + L"\n";
    }
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
        case WM_APP_STALL:
            // The whole machine stood still: Windows drops a low-level hook it could not deliver a callback to in
            // time, and tells nobody. Put ours back now rather than at the next keypress.
            win::debugLog("system stall: " + std::to_string(static_cast<unsigned>(wp)) + " ms; re-installing the hooks in case Windows dropped them");
            input_.reinstallHooks();
            return 0;
        case WM_APP_UIA:
            if (router_) router_->systemEvent(static_cast<HapticEvent>(wp), now());
            if (window_ && window_->isVisible()) syncModel();
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;  // sign-out, shutdown, or the installer's Restart Manager asking us to close
        case WM_APP_UPDATE: {
            std::unique_ptr<win::UpdateResult> r(reinterpret_cast<win::UpdateResult*>(lp));
            if (r) onUpdateResult(wp != 0, *r);
            return 0;
        }
        case WM_APP_HID:
            onDeviceChanged();
            return 0;
        case WM_APP_SETTINGS:
            if (modalDepth_ > 0) reloadPending_ = true;  // a file dialog is pumping messages with references into settings_
            else reloadSettings();
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
            showSettings();
            return 0;
        case WM_APP_TRAY: {
            const UINT event = LOWORD(lp);
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) showSettings();
            else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) showMenu();
            return 0;
        }
        case WM_TIMER: {
            const UINT_PTR id = wp;
            if (id == kTimerFlush) {
                if (++flushTicks_ % 5 == 0 && window_ && window_->isVisible()) syncModel();  // counters move without events
                store_->flushIfDue(now());
            } else if (id == kTimerWatchdog) {
                // Each hook is checked against its own raw stream: mouse input without mouse hook calls, or
                // keyboard input without keyboard hook calls, means Windows dropped that hook.
                const auto st = input_.status();
                const bool mouseDead = st.rawMouse != lastRawMouse_ && st.mouseHookCalls == lastMouseCalls_ && lastRawMouse_ != 0;
                const bool keyDead = st.rawKeyboard != lastRawKey_ && st.keyboardHookCalls == lastKeyCalls_ && lastRawKey_ != 0;
                if (mouseDead || keyDead) {
                    win::debugLog(std::string("watchdog: raw input flowing but no ") + (mouseDead ? "mouse" : "keyboard") + " hook callbacks; re-installing hooks");
                    input_.reinstallHooks();
                }
                lastMouseCalls_ = st.mouseHookCalls;
                lastKeyCalls_ = st.keyboardHookCalls;
                lastRawMouse_ = st.rawMouse;
                lastRawKey_ = st.rawKeyboard;
            } else if (id == kTimerCursor) {
                cursorTick();
            } else if (id == kTimerUpdateFirst) {
                KillTimer(hwnd_, kTimerUpdateFirst);
                scheduledUpdateCheck();
            } else if (id == kTimerUpdate) {
                scheduledUpdateCheck();
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
                quit();
            } else if (id == kTimerDetachExpiry) {
                KillTimer(hwnd_, kTimerDetachExpiry);
                pushConfig();  // shaping stops now unless the mouse came back
            } else if (id == kTimerPointerOwner) {
                KillTimer(hwnd_, kTimerPointerOwner);
                applyPointer();
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
            if (wp) quit();
            return 0;
        case WM_CLOSE:
            quit();
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
    if (cmdLine && wcsstr(cmdLine, L"--quit") != nullptr) {  // the uninstaller / scripts: ask the running instance to leave
        if (HWND other = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(other, WM_CLOSE, 0, 0);
            for (int i = 0; i < 50 && IsWindow(other); ++i) Sleep(100);
        }
        return 0;
    }
    const bool demo = cmdLine && wcsstr(cmdLine, L"--ring-demo") != nullptr;
    return app.run(inst, demo);
}
