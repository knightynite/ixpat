#include "win/input_thread.h"

#include <algorithm>
#include <cmath>

namespace ixpat::win {
namespace {

constexpr Seconds kSwallowReleaseWindow = 1.5;  // a swallowed press hides its release too, if it comes soon
constexpr Seconds kOtherWheelQuiet = 2.0;       // another device scrolled recently: do not claim unmatched wheel notches
constexpr Seconds kRawMatchWindow = 0.05;
constexpr double kDragThreshold = 20;
constexpr double kHoldMoveCancel = 4;
constexpr Seconds kCoastFrame = 1.0 / 120;
constexpr UINT WM_APP_REINSTALL = WM_APP + 0x40;

bool isChordKey(DWORD vk) {
    return vk == VK_LWIN || vk == VK_RWIN || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == 0x53 /*S*/ ||
           vk == 0x86 /*F23*/;
}

}  // namespace

InputThread* InputThread::instance_ = nullptr;

InputThread::InputThread() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFreq_ = static_cast<double>(f.QuadPart);
}

InputThread::~InputThread() { stop(); }

Seconds InputThread::now() const noexcept {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / qpcFreq_;
}

Seconds InputThread::lastPointerMove() const {
    return static_cast<double>(lastMoveQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
}

InputConfig InputThread::config() const noexcept {
    AcquireSRWLockShared(&cfgLock_);
    InputConfig c = cfg_;
    ReleaseSRWLockShared(&cfgLock_);
    return c;
}

void InputThread::setConfig(const InputConfig& c) {
    AcquireSRWLockExclusive(&cfgLock_);
    cfg_ = c;
    ReleaseSRWLockExclusive(&cfgLock_);
    raw_.anyMouse = c.anyMouse;  // read on the input thread; a bool, benign
}

void InputThread::setRingState(bool open, bool holdMode) {
    ringHold_ = holdMode;
    ringOpen_ = open;
}
void InputThread::gesturesBegin() { gestureRequested_ = true; }
void InputThread::gesturesEnd() { gestureRequested_ = false; }
void InputThread::reinstallHooks() {
    reinstall_ = true;
    if (wnd_) PostMessageW(wnd_, WM_APP_REINSTALL, 0, 0);
}

// MARK: ring buffer to main

void InputThread::post(InputEvent e) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % kRing;
    if (next == tail_.load(std::memory_order_acquire)) return;  // full: drop (main is wedged; never block here)
    ring_[head] = e;
    head_.store(next, std::memory_order_release);
    if (!posted_.exchange(true)) PostMessageW(main_, message_, 0, 0);
}

bool InputThread::pop(InputEvent& out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
        posted_ = false;
        // One more check closes the race with a push between the compare and the flag reset.
        if (tail == head_.load(std::memory_order_acquire)) return false;
        posted_ = true;
    }
    out = ring_[tail];
    tail_.store((tail + 1) % kRing, std::memory_order_release);
    return true;
}

// MARK: lifecycle

bool InputThread::start(HWND mainWindow, UINT message) {
    if (thread_.joinable()) return true;
    main_ = mainWindow;
    message_ = message;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    instance_ = this;
    // The ring is open in toggle mode: a press of the action button commits it instead of running its action
    // (the Mac's `button.pressIntercept`). The decoder hides the chord; main closes the ring on ringClick.
    chord_.pressIntercept = [this] {
        if (!(ringOpen_ && !ringHold_)) return false;
        post({InputEvent::Kind::ringClick, 0, 0, 0, now()});
        return true;
    };
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this, ready] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        WNDCLASSW wc{};
        wc.lpfnWndProc = &InputThread::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"iXpat.Input";
        RegisterClassW(&wc);
        wnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (wnd_) {
            raw_.registerOn(wnd_);
            hooksOk_ = installHooks();
        }
        SetEvent(ready);
        if (wnd_) loop();
        removeHooks();
        if (wnd_) {
            raw_.unregister();
            DestroyWindow(wnd_);
            wnd_ = nullptr;
        }
    });
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
    return wnd_ != nullptr;
}

void InputThread::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    if (wnd_) PostMessageW(wnd_, WM_NULL, 0, 0);
    thread_.join();
    CloseHandle(stopEvent_);
    CloseHandle(timer_);
    stopEvent_ = timer_ = nullptr;
    instance_ = nullptr;
}

bool InputThread::installHooks() {
    removeHooks();
    HINSTANCE self = GetModuleHandleW(nullptr);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &InputThread::mouseHook, self, 0);
    keyHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &InputThread::keyHook, self, 0);
    return mouseHook_ && keyHook_;
}

void InputThread::removeHooks() {
    if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
    if (keyHook_) UnhookWindowsHookEx(keyHook_);
    mouseHook_ = keyHook_ = nullptr;
}

LRESULT CALLBACK InputThread::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT && instance_) {
        if (auto e = instance_->raw_.decode(reinterpret_cast<HRAWINPUT>(lp))) instance_->onRaw(*e);
        return DefWindowProcW(h, msg, wp, lp);
    }
    if (msg == WM_APP_REINSTALL && instance_) {
        instance_->hooksOk_ = instance_->installHooks();
        instance_->reinstall_ = false;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void InputThread::loop() {
    refreshMonitors();
    HANDLE waits[2] = {stopEvent_, timer_};
    MSG m;
    for (;;) {
        DWORD timeoutMs = INFINITE;
        const Seconds t = now();
        if (holdArmed_) timeoutMs = static_cast<DWORD>(std::max(0.0, (holdDeadline_ - t) * 1000));
        if (t >= nextMonitorRefresh_) {
            refreshMonitors();
            nextMonitorRefresh_ = t + 5;
        }
        timeoutMs = std::min<DWORD>(timeoutMs, 5000);
        const DWORD r = MsgWaitForMultipleObjectsEx(2, waits, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) tick(now());  // coast frame
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) return;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
        if (holdArmed_ && now() >= holdDeadline_) fireHoldAction(now());
    }
}

void InputThread::refreshMonitors() noexcept {
    monitorCount_ = 0;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT r, LPARAM p) -> BOOL {
            auto* self = reinterpret_cast<InputThread*>(p);
            if (self->monitorCount_ < static_cast<int>(self->monitors_.size())) self->monitors_[self->monitorCount_++] = *r;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
}

// MARK: raw input (device side)

void InputThread::onRaw(const RawEvent& e) noexcept {
    rawCount_.fetch_add(1, std::memory_order_relaxed);
    const Seconds t = static_cast<double>(e.qpc) / qpcFreq_;
    // Remember recent entries for the hooks' content matching (never injected ones).
    if (!e.injected && (e.kind == RawKind::wheel || e.kind == RawKind::hwheel || e.kind == RawKind::key || e.kind == RawKind::button)) {
        RawEntry& slot = fifo_[(fifoHead_ + fifoCount_) % fifo_.size()];
        if (fifoCount_ == fifo_.size()) fifoHead_ = (fifoHead_ + 1) % fifo_.size();
        else ++fifoCount_;
        slot = RawEntry{e.kind, e.surfaceMouse, e.wheel, e.scan, e.buttonFlags, t};
    }
    switch (e.kind) {
        case RawKind::wheel:
            if (e.surfaceMouse) {
                lastSurfaceWheel_ = t;
                post({InputEvent::Kind::rawWheel, 0, e.wheel / WHEEL_DELTA, 0, t});
            } else if (!e.injected) {
                lastOtherWheel_ = t;
            }
            break;
        case RawKind::button:
            if (e.surfaceMouse) {
                std::uint8_t mask = surfaceButtons_;
                auto apply = [&](USHORT downFlag, USHORT upFlag, std::uint8_t bit) {
                    if (e.buttonFlags & downFlag) mask |= bit;
                    if (e.buttonFlags & upFlag) mask &= static_cast<std::uint8_t>(~bit);
                };
                apply(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 0x01);
                apply(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 0x02);
                apply(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 0x04);
                apply(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 0x08);
                apply(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 0x10);
                if (mask != surfaceButtons_) {
                    const std::uint8_t old = surfaceButtons_;
                    surfaceButtons_ = mask;
                    post({InputEvent::Kind::rawButtons, old, mask, 0, t});
                    if ((mask & ~old) & 0x01) {
                        const ChordOutput out = chord_.mouseLeftDown(t);
                        if (out != ChordOutput::none) post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
                    }
                }
            }
            break;
        case RawKind::key:
            if (e.surfaceMouse) {
                const ChordOutput out = chord_.key(e.vk, e.keyDown, t);
                post({InputEvent::Kind::rawKey, 0, e.vk, e.keyDown, t});
                if (out != ChordOutput::none) post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
            }
            break;
        default:
            break;
    }
}

void InputThread::drainRawInput() noexcept {
    MSG m;
    while (PeekMessageW(&m, wnd_, WM_INPUT, WM_INPUT, PM_REMOVE)) {
        if (auto e = raw_.decode(reinterpret_cast<HRAWINPUT>(m.lParam))) onRaw(*e);
        DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
    }
}

bool InputThread::matchRaw(RawKind kind, std::int16_t wheel, std::uint16_t scan, std::uint16_t buttonFlags, Seconds t,
                           bool& surface) noexcept {
    for (std::size_t i = 0; i < fifoCount_; ++i) {
        RawEntry& r = fifo_[(fifoHead_ + i) % fifo_.size()];
        if (r.kind != kind || std::abs(t - r.at) > kRawMatchWindow) continue;
        const bool same = kind == RawKind::key ? r.scan == scan : kind == RawKind::button ? r.buttonFlags == buttonFlags : r.wheel == wheel;
        if (!same) continue;
        surface = r.surface;
        r.kind = RawKind::other;  // consumed
        return true;
    }
    return false;
}

// MARK: hooks (system side)

LRESULT CALLBACK InputThread::mouseHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        instance_->heartbeat_.fetch_add(1, std::memory_order_relaxed);
        if (instance_->onMouse(wp, *reinterpret_cast<const MSLLHOOKSTRUCT*>(lp))) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK InputThread::keyHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        instance_->heartbeat_.fetch_add(1, std::memory_order_relaxed);
        if (instance_->onKey(wp, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp))) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool InputThread::onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept {
    const Seconds t = now();
    if (isOurs(m.dwExtraInfo)) return false;  // our own synthesised event
    drainRawInput();
    const InputConfig cfg = config();
    const bool injected = (m.flags & LLMHF_INJECTED) != 0;

    // Gesture mode: begin/end requested by main; the anchor is where the pointer sits when it begins.
    if (gestureRequested_ && !gestureActive_) {
        gestureActive_ = true;
        GetCursorPos(&gestureAnchor_);
        gestures_.begin();
    } else if (!gestureRequested_ && gestureActive_) {
        gestureActive_ = false;
        gestures_.end();
    }

    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            kinetic_.stop();  // a click stops the coast
            break;
        default:
            break;
    }

    switch (msg) {
        case WM_MOUSEMOVE: {
            LARGE_INTEGER c;
            QueryPerformanceCounter(&c);
            lastMoveQpc_.store(c.QuadPart, std::memory_order_relaxed);
            if (gestureActive_ && !injected) {
                // The pointer stays put: block the move, feed the travel to the accumulator.
                const double dx = m.pt.x - gestureAnchor_.x, dy = m.pt.y - gestureAnchor_.y;
                auto r = gestures_.move(dx, dy, cfg.gesture);
                for (const auto& a : r.actions) {
                    int idx = a == cfg.gesture.up ? 0 : a == cfg.gesture.down ? 1 : a == cfg.gesture.left ? 2 : 3;
                    post({InputEvent::Kind::gestureAction, 0, idx, 0, t});
                }
                if (cfg.gesture.freezePointer) return true;
                gestureAnchor_ = m.pt;  // not frozen: travel is per move
                return false;
            }
            if (leftDown_) {
                dragDistance_ += std::hypot(static_cast<double>(m.pt.x - dragStart_.x), static_cast<double>(m.pt.y - dragStart_.y));
                dragStart_ = m.pt;
                if (holdArmed_ && std::hypot(static_cast<double>(m.pt.x - holdOrigin_.x), static_cast<double>(m.pt.y - holdOrigin_.y)) > kHoldMoveCancel) {
                    holdArmed_ = false;  // it is a drag, not a press
                }
            }
            // Screen edge: once each time the pointer is pushed against a display edge.
            bool hit = false;
            for (int i = 0; i < monitorCount_; ++i) {
                const RECT& r = monitors_[i];
                if (m.pt.x < r.left - 1 || m.pt.x > r.right || m.pt.y < r.top - 1 || m.pt.y > r.bottom) continue;
                hit = m.pt.x <= r.left || m.pt.x >= r.right - 1 || m.pt.y <= r.top || m.pt.y >= r.bottom - 1;
                break;
            }
            if (hit != atEdge_) {
                atEdge_ = hit;
                if (hit) post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::screenEdge), 0, t});
            }
            return false;
        }
        case WM_LBUTTONDOWN: {
            bool surface = cfg.anyMouse;
            matchRaw(RawKind::button, 0, 0, RI_MOUSE_LEFT_BUTTON_DOWN, t, surface);
            if (ringOpen_ && !injected) {
                post({InputEvent::Kind::ringClick, 0, 0, 0, t});
                swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            if (cfg.swallowChords && chord_.swallowLeftClick(t) && !injected) {  // the firmware's fake click
                swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            leftDown_ = true;
            dragStart_ = m.pt;
            dragDistance_ = 0;
            holdFired_ = false;
            if (cfg.enabled && cfg.holdActionEnabled && cfg.holdAction != HoldAction::none && !injected &&
                (surface || cfg.anyMouse)) {
                holdArmed_ = true;
                holdOrigin_ = m.pt;
                holdDeadline_ = t + cfg.holdActionMs / 1000.0;
            }
            return false;
        }
        case WM_LBUTTONUP: {
            leftDown_ = false;
            holdArmed_ = false;
            if (holdFired_) {  // the hold action already released the button for the app
                holdFired_ = false;
                return true;
            }
            if (t < swallowLeftUpUntil_) {
                swallowLeftUpUntil_ = 0;
                return true;
            }
            if (dragDistance_ > kDragThreshold) {
                post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::dragEnd), 0, t});
                post({InputEvent::Kind::dragEnded, 0, m.pt.x, m.pt.y, t});
            }
            dragDistance_ = 0;
            return false;
        }
        case WM_RBUTTONDOWN: {
            holdArmed_ = false;
            if (ringOpen_ && !injected) {
                post({InputEvent::Kind::ringCancel, 0, 0, 0, t});
                swallowRightUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            return false;
        }
        case WM_RBUTTONUP:
            if (t < swallowRightUpUntil_) {
                swallowRightUpUntil_ = 0;
                return true;
            }
            return false;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            if (injected) return false;
            const auto delta = static_cast<std::int16_t>(HIWORD(m.mouseData));
            bool surface = false;
            const bool matched = matchRaw(msg == WM_MOUSEWHEEL ? RawKind::wheel : RawKind::hwheel, delta, 0, 0, t, surface);
            if (!matched) {
                // The hook ran before its WM_INPUT: the Mac's rule, while this mouse is live and no other
                // device scrolled recently, the notch is ours.
                surface = cfg.anyMouse || (cfg.shapeWheel && t - lastOtherWheel_ > kOtherWheelQuiet);
            }
            if (!surface || !cfg.shapeWheel || !cfg.enabled) return false;
            const double notches = static_cast<double>(delta) / WHEEL_DELTA;
            if (cfg.kinetic.enabled) {
                if (msg == WM_MOUSEWHEEL) kinetic_.notch(notches, 0, t, cfg.kinetic);
                else kinetic_.notch(0, notches, t, cfg.kinetic);
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(kCoastFrame * 1e7);
                SetWaitableTimer(timer_, &due, static_cast<LONG>(kCoastFrame * 1000), nullptr, nullptr, FALSE);
                return true;
            }
            if (cfg.reverseOnly) {
                if (msg == WM_MOUSEWHEEL) sendWheelUnits(-delta, 0);
                else sendWheelUnits(0, -delta);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool InputThread::onKey(WPARAM msg, const KBDLLHOOKSTRUCT& k) noexcept {
    const Seconds t = now();
    if (isOurs(k.dwExtraInfo)) return false;
    drainRawInput();
    const InputConfig cfg = config();
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) kinetic_.stop();  // a key stops the coast
    // Escape cancels an open ring (and is swallowed); any other key passes while the ring is open.
    if (ringOpen_ && k.vkCode == VK_ESCAPE && !(k.flags & LLKHF_INJECTED)) {
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) post({InputEvent::Kind::ringCancel, 0, 0, 0, t});
        return true;
    }
    if (!cfg.swallowChords || !cfg.enabled) return false;
    if (!isChordKey(k.vkCode)) return false;
    bool surface = false;
    const bool matched = matchRaw(RawKind::key, 0, static_cast<std::uint16_t>(k.scanCode), 0, t, surface);
    // The chords arrive as Win+Shift+S / Win+Shift+F23: swallow their keys only while the mouse is sending one.
    if (matched) return surface && chord_.chordActive(t);
    return chord_.chordActive(t) && !(k.flags & LLKHF_INJECTED);
}

// MARK: timers

void InputThread::tick(Seconds t) noexcept {
    const auto step = kinetic_.tick(t);
    if (!step.active) {
        CancelWaitableTimer(timer_);
        wheelOut_.reset();
        return;
    }
    if (step.dy || step.dx) wheelOut_.emit(step.dy, step.dx, config().kinetic.pxPerNotch);
}

void InputThread::fireHoldAction(Seconds t) noexcept {
    holdArmed_ = false;
    if (!leftDown_) return;
    const InputConfig cfg = config();
    post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::forceClick), 0, t});
    switch (cfg.holdAction) {
        case HoldAction::contextMenu:
            // Let the app see the press end where it began, then a right click; the real release is hidden.
            sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false);
            sendMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, true);
            sendMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, false);
            holdFired_ = true;
            break;
        case HoldAction::middleClick:
            sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false);
            sendMouseButton(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, true);
            sendMouseButton(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, false);
            holdFired_ = true;
            break;
        case HoldAction::actionRing:
            sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false);
            holdFired_ = true;
            post({InputEvent::Kind::ringOpenRequest, 0, 0, 0, t});
            break;
        default:
            break;
    }
}

}  // namespace ixpat::win
