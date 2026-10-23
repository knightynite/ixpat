#include "win/input_thread.h"

#include <algorithm>
#include <cmath>

namespace ixpat::win {
namespace {

constexpr Seconds kSwallowReleaseWindow = 1.5;  // a swallowed press hides its release too, if it comes soon
constexpr Seconds kOtherDeviceQuiet = 2.0;      // another device acted recently: do not claim unmatched events
constexpr Seconds kRawMatchWindow = 0.05;
constexpr double kDragThreshold = 20;
constexpr double kHoldMoveCancel = 4;
constexpr Seconds kCoastFrame = 1.0 / 120;
constexpr int kDrainBudget = 32;
constexpr UINT WM_APP_REINSTALL = WM_APP + 0x40;

bool isChordKey(DWORD vk) {
    return vk == VK_LWIN || vk == VK_RWIN || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == 0x53 /*S*/ ||
           vk == 0x86 /*F23*/;
}

std::uint16_t buttonFlagFor(WPARAM msg, DWORD mouseData) noexcept {
    switch (msg) {
        case WM_LBUTTONDOWN: return RI_MOUSE_LEFT_BUTTON_DOWN;
        case WM_LBUTTONUP: return RI_MOUSE_LEFT_BUTTON_UP;
        case WM_RBUTTONDOWN: return RI_MOUSE_RIGHT_BUTTON_DOWN;
        case WM_RBUTTONUP: return RI_MOUSE_RIGHT_BUTTON_UP;
        case WM_MBUTTONDOWN: return RI_MOUSE_MIDDLE_BUTTON_DOWN;
        case WM_MBUTTONUP: return RI_MOUSE_MIDDLE_BUTTON_UP;
        case WM_XBUTTONDOWN: return HIWORD(mouseData) == XBUTTON1 ? RI_MOUSE_BUTTON_4_DOWN : RI_MOUSE_BUTTON_5_DOWN;
        case WM_XBUTTONUP: return HIWORD(mouseData) == XBUTTON1 ? RI_MOUSE_BUTTON_4_UP : RI_MOUSE_BUTTON_5_UP;
        default: return 0;
    }
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
    InputConfig c = cfg_;  // plain data: no allocation under the lock
    ReleaseSRWLockShared(&cfgLock_);
    return c;
}

void InputThread::setConfig(const InputConfig& c) {
    bool nowDisabled = false;
    AcquireSRWLockExclusive(&cfgLock_);
    nowDisabled = cfg_.enabled && !c.enabled;
    cfg_ = c;
    ReleaseSRWLockExclusive(&cfgLock_);
    anyMouseRequested_ = c.anyMouse;
    if (nowDisabled) disableRequested_ = true;  // applied on the input thread
}

void InputThread::setRingState(bool open, bool holdMode) {
    ringHold_ = holdMode;
    ringOpen_ = open;
}
void InputThread::gesturesBegin() { gestureRequested_ = true; }
void InputThread::gesturesEnd() { gestureRequested_ = false; }
void InputThread::reinstallHooks() {
    if (HWND w = wnd_.load()) PostMessageW(w, WM_APP_REINSTALL, 0, 0);
}

InputStatus InputThread::status() const {
    InputStatus s;
    s.rawRegistered = rawOk_.load();
    s.mouseHook = mouseOk_.load();
    s.keyboardHook = keyOk_.load();
    s.mouseHookCalls = mouseCalls_.load(std::memory_order_relaxed);
    s.keyboardHookCalls = keyCalls_.load(std::memory_order_relaxed);
    s.rawMouse = rawMouse_.load(std::memory_order_relaxed);
    s.rawKeyboard = rawKeyboard_.load(std::memory_order_relaxed);
    return s;
}

// MARK: ring buffer to main

void InputThread::post(InputEvent e) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % kRing;
    if (next == tail_.load(std::memory_order_acquire)) {  // full: main is wedged; never block here
        overflow_ = true;
        return;
    }
    ring_[head] = e;
    head_.store(next, std::memory_order_release);
    if (!posted_.exchange(true)) {
        if (!PostMessageW(main_, message_, 0, 0)) posted_ = false;  // retry on the next event
    }
}

bool InputThread::pop(InputEvent& out) {
    if (overflow_.exchange(false)) {
        out = InputEvent{InputEvent::Kind::overflow, 0, 0, 0, now()};
        return true;
    }
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
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    instance_ = this;
    // The ring is open in toggle mode: a press of the action button commits it instead of running its
    // action (the Mac's `button.pressIntercept`). The decoder hides the chord; main closes the ring.
    chord_.pressIntercept = [this] {
        if (!(ringOpen_ && !ringHold_)) return false;
        post({InputEvent::Kind::ringClick, 0, 0, 0, now()});
        return true;
    };
    thread_ = std::thread([this] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        WNDCLASSW wc{};
        wc.lpfnWndProc = &InputThread::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"iXpat.Input";
        RegisterClassW(&wc);
        HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (w) {
            raw_.setAnyMouse(anyMouseRequested_.load());
            rawOk_ = raw_.registerOn(w);
            raw_.resolveAll();
            installHooks();
        }
        wnd_.store(w);
        SetEvent(ready_);
        if (w) loop();
        removeHooks();
        if (w) {
            raw_.unregister();
            wnd_.store(nullptr);
            DestroyWindow(w);
        }
    });
    if (WaitForSingleObject(ready_, 5000) != WAIT_OBJECT_0) {
        // The worker never came up: tear it down before anything else runs on this object.
        stop();
        return false;
    }
    return wnd_.load() != nullptr;
}

void InputThread::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    if (HWND w = wnd_.load()) PostMessageW(w, WM_NULL, 0, 0);
    thread_.join();
    CloseHandle(stopEvent_);
    CloseHandle(ready_);
    CloseHandle(timer_);
    stopEvent_ = ready_ = timer_ = nullptr;
    instance_ = nullptr;
}

bool InputThread::installHooks() {
    removeHooks();
    HINSTANCE self = GetModuleHandleW(nullptr);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &InputThread::mouseHook, self, 0);
    keyHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &InputThread::keyHook, self, 0);
    if (!mouseHook_ || !keyHook_) removeHooks();  // all or nothing: a half-installed pair would shape some events but not others
    mouseOk_ = mouseHook_ != nullptr;
    keyOk_ = keyHook_ != nullptr;
    return mouseHook_ && keyHook_;
}

void InputThread::removeHooks() {
    if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
    if (keyHook_) UnhookWindowsHookEx(keyHook_);
    mouseHook_ = keyHook_ = nullptr;
    mouseOk_ = keyOk_ = false;
}

LRESULT CALLBACK InputThread::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!instance_) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_INPUT:
            instance_->handleRawMessage(reinterpret_cast<HRAWINPUT>(lp), true);
            return DefWindowProcW(h, msg, wp, lp);  // required after RIDEV_INPUTSINK delivery
        case WM_INPUT_DEVICE_CHANGE:
            instance_->raw_.deviceChanged(wp, reinterpret_cast<HANDLE>(lp));
            return 0;
        case WM_APP_REINSTALL:
            instance_->installHooks();
            return 0;
        default:
            return DefWindowProcW(h, msg, wp, lp);
    }
}

void InputThread::loop() {
    refreshMonitors();
    HANDLE waits[2] = {stopEvent_, timer_};
    MSG m;
    for (;;) {
        DWORD timeoutMs = 5000;
        const Seconds t = now();
        if (holdArmed_) timeoutMs = static_cast<DWORD>(std::max(0.0, std::min(5.0, holdDeadline_ - t) * 1000));
        if (t >= nextMonitorRefresh_) {
            refreshMonitors();
            nextMonitorRefresh_ = t + 5;
        }
        const DWORD r = MsgWaitForMultipleObjectsEx(2, waits, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) tick(now());  // coast frame
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) return;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
        if (disableRequested_.exchange(false)) applyDisable();
        if (anyMouseRequested_.load() != cfg_.anyMouse) raw_.setAnyMouse(anyMouseRequested_.load());
        if (holdArmed_ && now() >= holdDeadline_) fireHoldAction(now());
    }
}

void InputThread::applyDisable() noexcept {
    kinetic_.stop();
    CancelWaitableTimer(timer_);
    holdArmed_ = false;
    endGestureNow();
}

void InputThread::endGestureNow() noexcept {
    if (gestureActive_) {
        gestures_.end();
        gestureActive_ = false;
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

// MARK: attribution bookkeeping

bool InputThread::matchRaw(const RawEntry& probe, Seconds t, bool& surface) noexcept {
    for (std::size_t i = 0; i < fifoCount_; ++i) {
        RawEntry& r = fifo_[(fifoHead_ + i) % fifo_.size()];
        if (r.kind != probe.kind || std::abs(t - r.at) > kRawMatchWindow) continue;
        bool same = false;
        switch (probe.kind) {
            case RawKind::key: same = r.scan == probe.scan && r.keyDown == probe.keyDown && r.keyE0 == probe.keyE0; break;
            case RawKind::button: same = (r.buttonFlags & probe.buttonFlags) != 0; break;
            default: same = r.wheel == probe.wheel; break;
        }
        if (!same) continue;
        surface = r.surface;
        r.kind = RawKind::other;  // consumed
        return true;
    }
    return false;
}

void InputThread::expectRaw(const RawEntry& probe) noexcept {
    expected_[expectedNext_] = probe;
    expectedNext_ = (expectedNext_ + 1) % expected_.size();
}

bool InputThread::retireExpected(const RawEntry& raw) noexcept {
    for (auto& e : expected_) {
        if (e.kind != raw.kind || std::abs(raw.at - e.at) > kRawMatchWindow) continue;
        bool same = false;
        switch (raw.kind) {
            case RawKind::key: same = e.scan == raw.scan && e.keyDown == raw.keyDown && e.keyE0 == raw.keyE0; break;
            case RawKind::button: same = (e.buttonFlags & raw.buttonFlags) != 0; break;
            default: same = e.wheel == raw.wheel; break;
        }
        if (!same) continue;
        e.kind = RawKind::other;
        return true;  // its hook event already decided: do not let it pair with a later one
    }
    return false;
}

// MARK: raw input (device side)

void InputThread::handleRawMessage(HRAWINPUT h, bool allowResolve) noexcept {
    RawEvent parts[3];
    const int n = raw_.decode(h, parts, allowResolve);
    for (int i = 0; i < n; ++i) onRaw(parts[i]);
}

void InputThread::onRaw(const RawEvent& e) noexcept {
    const Seconds t = static_cast<double>(e.qpc) / qpcFreq_;
    if (e.kind == RawKind::key) rawKeyboard_.fetch_add(1, std::memory_order_relaxed);
    else rawMouse_.fetch_add(1, std::memory_order_relaxed);
    // Remember recent physical entries for the hooks' content matching, unless a hook already decided this one.
    if (!e.injected && (e.kind == RawKind::wheel || e.kind == RawKind::hwheel || e.kind == RawKind::key || e.kind == RawKind::button)) {
        RawEntry entry{e.kind, e.surfaceMouse, e.wheel, e.scan, e.keyDown, e.keyE0, e.buttonFlags, t};
        if (!retireExpected(entry)) {
            RawEntry& slot = fifo_[(fifoHead_ + fifoCount_) % fifo_.size()];
            if (fifoCount_ == fifo_.size()) fifoHead_ = (fifoHead_ + 1) % fifo_.size();
            else ++fifoCount_;
            slot = entry;
        }
    }
    switch (e.kind) {
        case RawKind::move:
            if (!e.injected && !e.unknownDevice) {
                if (!moverKnown_ || lastMoverSurface_ != e.surfaceMouse) {
                    moverKnown_ = true;
                    lastMoverSurface_ = e.surfaceMouse;
                    post({InputEvent::Kind::pointerOwner, 0, e.surfaceMouse ? 1 : 0, 0, t});
                }
            }
            break;
        case RawKind::wheel:
        case RawKind::hwheel:
            if (e.surfaceMouse) {
                lastSurfaceWheel_ = t;
                if (e.kind == RawKind::wheel) post({InputEvent::Kind::rawWheel, 0, e.wheel / WHEEL_DELTA, 0, t});
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
            } else if (!e.injected) {
                lastOtherButton_ = t;
            }
            break;
        case RawKind::key:
            if (e.surfaceMouse) {
                const ChordOutput out = chord_.key(e.vk, e.keyDown, t);
                post({InputEvent::Kind::rawKey, 0, e.vk, e.keyDown, t});
                if (out != ChordOutput::none) {
                    // A hold ends on the input thread at once, so moves after the physical release never
                    // feed the accumulator (main's gesturesEnd() follows and is idempotent).
                    if (out == ChordOutput::holdEnd) endGestureNow();
                    post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
                }
            }
            break;
        default:
            break;
    }
}

void InputThread::drainRawInput() noexcept {
    // Only from the outermost hook callback: PeekMessage can deliver another hook event while we drain,
    // and a nested drain would interleave the FIFO bookkeeping.
    if (hookDepth_ != 1) return;
    HWND w = wnd_.load(std::memory_order_relaxed);
    if (!w) return;
    MSG m;
    int budget = kDrainBudget;
    while (budget-- > 0 && PeekMessageW(&m, w, WM_INPUT, WM_INPUT, PM_REMOVE | PM_NOYIELD)) {
        handleRawMessage(reinterpret_cast<HRAWINPUT>(m.lParam), false);
        DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
    }
}

// MARK: hooks (system side)

LRESULT CALLBACK InputThread::mouseHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        instance_->mouseCalls_.fetch_add(1, std::memory_order_relaxed);
        ++instance_->hookDepth_;
        const bool swallow = instance_->onMouse(wp, *reinterpret_cast<const MSLLHOOKSTRUCT*>(lp));
        --instance_->hookDepth_;
        if (swallow) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK InputThread::keyHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        instance_->keyCalls_.fetch_add(1, std::memory_order_relaxed);
        ++instance_->hookDepth_;
        const bool swallow = instance_->onKey(wp, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp));
        --instance_->hookDepth_;
        if (swallow) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool InputThread::onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept {
    const Seconds t = now();
    if (isOurs(m.dwExtraInfo)) return false;  // our own synthesised event
    drainRawInput();
    if (disableRequested_.exchange(false)) applyDisable();
    const InputConfig cfg = config();
    const bool injected = (m.flags & LLMHF_INJECTED) != 0;

    // Gesture mode: begin/end requested by main; the anchor is where the pointer sits when it begins.
    if (gestureRequested_ && !gestureActive_ && cfg.enabled) {
        gestureActive_ = true;
        ++gestureSession_;
        GetCursorPos(&gestureAnchor_);
        gestures_.begin();
    } else if (!gestureRequested_ && gestureActive_) {
        endGestureNow();
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
                const auto r = gestures_.move(dx, dy, cfg.gesture);
                for (int i = 0; i < r.count; ++i) post({InputEvent::Kind::gestureAction, 0, r.dirs[static_cast<std::size_t>(i)], gestureSession_, t});
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
                if (hit && cfg.enabled) post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::screenEdge), 0, t});
            }
            return false;
        }
        case WM_LBUTTONDOWN: {
            bool surface = false;
            RawEntry probe{RawKind::button, false, 0, 0, false, false, RI_MOUSE_LEFT_BUTTON_DOWN, t};
            bool matched = injected ? false : matchRaw(probe, t, surface);
            if (!matched && !injected) {
                // Hook before raw: this mouse is live and nothing else clicked lately -> ours; the raw entry is retired when it lands.
                surface = cfg.anyMouse || (cfg.shapeWheel && t - lastOtherButton_ > kOtherDeviceQuiet);
                expectRaw(probe);
            }
            if (ringOpen_ && !injected) {
                post({InputEvent::Kind::ringClick, 0, 0, 0, t});
                swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            if (cfg.swallowChords && cfg.enabled && chord_.swallowLeftClick(t) && !injected) {  // the firmware's fake click
                swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            leftDown_ = true;
            dragStart_ = m.pt;
            dragDistance_ = 0;
            holdFired_ = false;
            if (cfg.enabled && cfg.holdActionEnabled && cfg.holdAction != HoldAction::none && !injected && surface) {
                holdArmed_ = true;
                holdOrigin_ = m.pt;
                holdDeadline_ = t + cfg.holdActionMs / 1000.0;
            }
            return false;
        }
        case WM_LBUTTONUP: {
            leftDown_ = false;
            holdArmed_ = false;
            if (!injected) {
                RawEntry probe{RawKind::button, false, 0, 0, false, false, RI_MOUSE_LEFT_BUTTON_UP, t};
                bool s = false;
                if (!matchRaw(probe, t, s)) expectRaw(probe);
            }
            if (holdFired_) {  // the hold action already released the button for the app
                holdFired_ = false;
                return true;
            }
            if (t < swallowLeftUpUntil_) {
                swallowLeftUpUntil_ = 0;
                return true;
            }
            if (dragDistance_ > kDragThreshold && cfg.enabled) {
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
            RawEntry probe{msg == WM_MOUSEWHEEL ? RawKind::wheel : RawKind::hwheel, false, delta, 0, false, false, 0, t};
            bool surface = false;
            if (!matchRaw(probe, t, surface)) {
                // The hook ran before its WM_INPUT: the Mac's rule, while this mouse is live and no other
                // device scrolled recently, the notch is ours. The late raw entry is retired when it lands.
                surface = cfg.anyMouse || (cfg.shapeWheel && t - lastOtherWheel_ > kOtherDeviceQuiet);
                expectRaw(probe);
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
    const bool injected = (k.flags & LLKHF_INJECTED) != 0;
    drainRawInput();
    if (disableRequested_.exchange(false)) applyDisable();
    const InputConfig cfg = config();
    const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (down) kinetic_.stop();  // a key stops the coast
    if (injected) return false;  // another program's synthetic keys: never attributed, never swallowed
    // Escape cancels an open ring (and is swallowed); any other key passes while the ring is open.
    if (ringOpen_ && k.vkCode == VK_ESCAPE) {
        if (down) post({InputEvent::Kind::ringCancel, 0, 0, 0, t});
        return true;
    }
    if (!isChordKey(k.vkCode)) return false;
    RawEntry probe{RawKind::key, false, 0, static_cast<std::uint16_t>(k.scanCode), down, (k.flags & LLKHF_EXTENDED) != 0, 0, t};
    bool surface = false;
    const bool matched = matchRaw(probe, t, surface);
    if (!matched) expectRaw(probe);
    if (!cfg.swallowChords || !cfg.enabled) return false;
    // The chords arrive as Win+Shift+S / Win+Shift+F23: swallow their keys only while the mouse is sending one.
    if (matched) return surface && chord_.chordActive(t);
    return chord_.chordActive(t);
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
    if (!cfg.enabled || !cfg.holdActionEnabled) return;  // disabled since the press: do nothing
    post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::forceClick), 0, t});
    switch (cfg.holdAction) {
        case HoldAction::contextMenu:
            // Let the app see the press end where it began, then a right click; the real release is hidden
            // only if the synthetic release went through.
            if (sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false)) {
                holdFired_ = true;
                sendMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, true);
                sendMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, false);
            }
            break;
        case HoldAction::middleClick:
            if (sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false)) {
                holdFired_ = true;
                sendMouseButton(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, true);
                sendMouseButton(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, false);
            }
            break;
        case HoldAction::actionRing:
            if (sendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false)) holdFired_ = true;
            post({InputEvent::Kind::ringOpenRequest, 0, 0, 0, t});
            break;
        default:
            break;
    }
}

}  // namespace ixpat::win
