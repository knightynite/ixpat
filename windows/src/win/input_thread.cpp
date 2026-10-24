#include "win/input_thread.h"

#include <algorithm>
#include <cmath>

namespace ixpat::win {
namespace {

constexpr Seconds kSwallowReleaseWindow = 1.5;  // a swallowed press hides its release too, if it comes soon
constexpr Seconds kOtherDeviceQuiet = 2.0;      // another device acted recently: do not claim unmatched events
constexpr Seconds kRawMatchWindow = 0.05;
constexpr Seconds kHookTwinWindow = 0.03;    // a report and its hook callback lie this close (keyboard: hook first; mouse: report first)
constexpr Seconds kHookAnswerWindow = 0.05;  // a physical report still without a hook callback after this: Windows dropped the hook
constexpr Seconds kHookRefresh = 5.0;        // while idle, the hooks are put back this often: a dropped hook never waits for a key
constexpr Seconds kForgetSwallowed = 2.0;    // a swallowed make whose break never came: its key is the user's again after this
constexpr double kDragThreshold = 20;
constexpr double kHoldMoveCancel = 4;
constexpr Seconds kCoastFrame = 1.0 / 120;
constexpr int kDrainBudget = 32;
constexpr UINT WM_APP_REINSTALL = WM_APP + 0x40;

bool isChordKey(DWORD vk) {
    return vk == VK_LWIN || vk == VK_RWIN || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == 0x53 /*S*/ ||
           vk == 0x86 /*F23*/;
}

/// Raw Input reports VK_SHIFT where the hook reports VK_LSHIFT / VK_RSHIFT: one index for both views of a key.
int chordIndex(DWORD vk) {
    const int v = static_cast<int>(vk & 0xFF);
    return (v == kVkLShift || v == kVkRShift) ? kVkShift : v;
}

bool isLetter(int vk) { return vk == kVkS || vk == kVkF23; }

/// A popup menu (#32768) or a combo box's list under the pointer: these scroll a whole item per event and
/// stutter under a stream of fractional deltas, so the wheel passes through there untouched.
bool isMenuUnder(POINT pt) noexcept {
    HWND w = WindowFromPoint(pt);
    if (!w) return false;
    wchar_t cls[32];
    const int n = GetClassNameW(w, cls, 32);
    if (n <= 0) return false;
    return wcscmp(cls, L"#32768") == 0 || wcscmp(cls, L"ComboLBox") == 0;
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

void raiseMax(std::atomic<std::uint32_t>& slot, std::uint32_t value) noexcept {
    std::uint32_t cur = slot.load(std::memory_order_relaxed);
    while (value > cur && !slot.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }
}

}  // namespace

InputThread* InputThread::instance_ = nullptr;

InputThread::InputThread() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFreq_ = static_cast<double>(f.QuadPart);
    swallowedAt_.fill(-1e9);
}

InputThread::~InputThread() { stop(); }

std::int64_t InputThread::qpcNow() const noexcept {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

Seconds InputThread::now() const noexcept { return static_cast<double>(qpcNow()) / qpcFreq_; }

Seconds InputThread::lastPointerMove() const {
    // The Surface Mouse's own motion: the accessibility and cursor-shape ticks follow this mouse, not every device.
    return static_cast<double>(lastSurfaceMoveQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
}

InputConfig InputThread::config() const noexcept {
    return cfgSlots_[cfgIndex_.load(std::memory_order_acquire)];  // a copy of the published slot, no lock to wait on
}

void InputThread::setConfig(const InputConfig& c) {
    const bool nowDisabled = cfgLast_.enabled && !c.enabled;
    cfgLast_ = c;
    const std::size_t next = (cfgIndex_.load(std::memory_order_relaxed) + 1) % kConfigSlots;
    cfgSlots_[next] = c;
    cfgIndex_.store(next, std::memory_order_release);
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
    s.physicalMouse = physMouse_.load(std::memory_order_relaxed);
    s.physicalKeyboard = physKeyboard_.load(std::memory_order_relaxed);
    s.maxHookMicros = maxHookMicros_.load(std::memory_order_relaxed);
    s.maxChainMicros = maxChainMicros_.load(std::memory_order_relaxed);
    s.maxHookLagMs = maxHookLagMs_.load(std::memory_order_relaxed);
    s.hookReinstalls = reinstalls_.load(std::memory_order_relaxed);
    s.hookInstallError = installError_.load(std::memory_order_relaxed);
    s.maxCoastGapMs = maxCoastGapMs_.load(std::memory_order_relaxed);
    s.maskFailures = maskFailures_.load(std::memory_order_relaxed);
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
    // The ring is open in toggle mode: the gesture (a press's letter, the double press's click) commits it instead
    // of running its action (the Mac's `button.pressIntercept`). The decoder hides the chord; main closes the ring.
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
            anyMouse_ = anyMouseRequested_.load();
            raw_.setAnyMouse(anyMouse_);
            rawOk_ = raw_.registerOn(w);
            raw_.resolveAll();
            installHooks();
            lastHookRefresh_ = now();
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
    DWORD err = 0;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &InputThread::mouseHook, self, 0);
    if (!mouseHook_) err = GetLastError();
    keyHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &InputThread::keyHook, self, 0);
    if (!keyHook_ && !err) err = GetLastError();
    if (!mouseHook_ || !keyHook_) removeHooks();  // all or nothing: a half-installed pair would shape some events but not others
    mouseOk_ = mouseHook_ != nullptr;
    keyOk_ = keyHook_ != nullptr;
    installError_.store(err, std::memory_order_relaxed);
    // Alive from this moment: reports before now are not theirs to answer, and callbacks before now vouch for nothing.
    const std::int64_t q = qpcNow();
    lastMouseHookQpc_.store(q, std::memory_order_relaxed);
    lastKeyHookQpc_.store(q, std::memory_order_relaxed);
    rawMouseUnanswered_ = rawKeyUnanswered_ = 0;
    for (auto& s : hookMake_) s.used = true;
    for (auto& s : hookBreak_) s.used = true;
    lastHookRefresh_ = now();
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
            // Both hook losses seen so far came with the first press after the mouse reconnected. A program that
            // re-installs its own hooks on a device change goes ahead of ours in the chain, and one that does not
            // chain starves everything behind it: so ours go back to the head a moment after any device change.
            instance_->rehookAt_ = instance_->now() + 1.0;
            return 0;
        case WM_APP_REINSTALL:
            instance_->installHooks();
            instance_->reinstalls_.fetch_add(1, std::memory_order_relaxed);
            instance_->post({InputEvent::Kind::hooksReinstalled, 0, 0, static_cast<int>(instance_->installError_.load(std::memory_order_relaxed)),
                             instance_->now()});
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
        DWORD timeoutMs = 1000;
        const Seconds t = now();
        if (holdArmed_) timeoutMs = static_cast<DWORD>(std::max(0.0, std::min(1.0, holdDeadline_ - t) * 1000));
        if (rawMouseUnanswered_ > 0 || rawKeyUnanswered_ > 0) timeoutMs = std::min<DWORD>(timeoutMs, 50);  // a report awaits its hook callback
        if (rehookAt_ > 0) timeoutMs = std::min(timeoutMs, static_cast<DWORD>(std::max(0.0, std::min(1.0, rehookAt_ - t) * 1000)));
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
        if (anyMouseRequested_.load() != anyMouse_) {
            anyMouse_ = anyMouseRequested_.load();
            raw_.setAnyMouse(anyMouse_);
        }
        if (holdArmed_ && now() >= holdDeadline_) fireHoldAction(now());
        flushMask();
        checkHookLiveness();
        // While nothing is in flight, put the hooks back every few seconds: Windows drops them silently and tells
        // nobody, and a dropped hook must not wait for the next key to be found out.
        const Seconds t2 = now();
        const Seconds sinceKey = t2 - static_cast<double>(lastKeyHookQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
        const bool due = (t2 - lastHookRefresh_ >= kHookRefresh) || (rehookAt_ > 0 && t2 >= rehookAt_);
        if (due && sinceKey > 0.2 && !chord_.chordActive(t2) && !leftDown_ && fedMake_.none() && swallowedMake_.none() && letterPassed_.none() &&
            !holdArmed_ && !gestureActive_) {
            installHooks();  // only with the keyboard quiet: a refresh voids the observation a report in flight still needs
            rehookAt_ = 0;
        }
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

// MARK: the chord decoder (fed from the hooks and from attributed Raw Input, fires only on what it heard)

void InputThread::feedChord(int vk, bool down, Seconds t) noexcept {
    const ChordOutput out = chord_.key(vk, down, t);
    if (out == ChordOutput::none) return;
    // A hold ends on the input thread at once, so moves after the physical release never feed the
    // accumulator (main's gesturesEnd() follows and is idempotent).
    if (out == ChordOutput::holdEnd) endGestureNow();
    post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
}

void InputThread::forgetStaleChord(Seconds t) noexcept {
    // The decoder forgets a chord whose breaks never came once the mouse has been quiet; the bookkeeping that
    // mirrors it (which makes it heard, which it ate) goes with it, so the next chord starts clean and a
    // break that still turns up passes as the user's. A letter Windows holds is not the chord's to forget.
    if (!chord_.stale(t)) return;
    fedMake_.reset();
    swallowedMake_.reset();
}

void InputThread::flushMask() noexcept {
    if (!maskPending_) return;
    maskPending_ = false;
    if (!sendMaskKey()) maskFailures_.fetch_add(1, std::memory_order_relaxed);
}

// MARK: hook liveness

void InputThread::noteRawForLiveness(bool mouse, Seconds at) noexcept {
    // Every physical report has a hook callback within a few microseconds of it: for the mouse the report is posted
    // first and the callback follows, for the keyboard the callback runs first and the report is generated after it
    // returns. A report with no callback near it starts a clock; checkHookLiveness() reads it.
    Seconds& unanswered = mouse ? rawMouseUnanswered_ : rawKeyUnanswered_;
    if (unanswered > 0) return;
    const auto& lastHook = mouse ? lastMouseHookQpc_ : lastKeyHookQpc_;
    if (static_cast<double>(lastHook.load(std::memory_order_relaxed)) / qpcFreq_ < at - kHookTwinWindow) unanswered = at;
}

void InputThread::checkHookLiveness() noexcept {
    const Seconds t = now();
    auto dead = [&](Seconds& unanswered, const std::atomic<std::int64_t>& lastHook) {
        if (unanswered <= 0) return false;
        if (static_cast<double>(lastHook.load(std::memory_order_relaxed)) / qpcFreq_ >= unanswered - kHookTwinWindow) {
            unanswered = 0;  // its callback came (the mouse hook's a moment after the report)
            return false;
        }
        if (t - unanswered < kHookAnswerWindow) return false;
        unanswered = 0;
        return true;
    };
    const bool mouseDead = dead(rawMouseUnanswered_, lastMouseHookQpc_);
    const bool keyDead = dead(rawKeyUnanswered_, lastKeyHookQpc_);
    if (!mouseDead && !keyDead) return;
    // Windows removed a hook (it does so silently when a callback, ours or one further down the chain, stalls).
    installHooks();
    reinstalls_.fetch_add(1, std::memory_order_relaxed);
    post({InputEvent::Kind::hooksReinstalled, 0, (mouseDead ? 1 : 0) | (keyDead ? 2 : 0),
          static_cast<int>(installError_.load(std::memory_order_relaxed)), t});
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
    if (!e.injected) {
        (e.kind == RawKind::key ? physKeyboard_ : physMouse_).fetch_add(1, std::memory_order_relaxed);
        noteRawForLiveness(e.kind != RawKind::key, t);
    }
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
            if (e.surfaceMouse) lastSurfaceMoveQpc_.store(e.qpc, std::memory_order_relaxed);
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
                    post({InputEvent::Kind::rawButtons, old, mask, 0, t});  // haptics only; the fake click is the hook's to judge
                }
            } else if (!e.injected) {
                lastOtherButton_ = t;
            }
            break;
        case RawKind::key: {
            if (!isChordKey(e.vk)) break;
            if (!e.surfaceMouse) {
                if (!e.injected) post({InputEvent::Kind::rawKey, 1, e.vk, e.keyDown ? 1 : 0, t});
                break;
            }
            // The mouse's keyboard collection. Its letters never show here (the hook ate them, or kbdhid dropped
            // them); its Shift and Win do, about a millisecond after the hook passed them. The one callback that
            // belongs to this report vouches for it, once; a report with none means the hook is gone and Windows has
            // already acted on this chord: the decoder stays silent, and checkHookLiveness() puts the hooks back.
            forgetStaleChord(t);
            const int idx = chordIndex(e.vk);
            const auto slot = static_cast<std::size_t>(idx);
            HookSeen& seen = (e.keyDown ? hookMake_ : hookBreak_)[slot];
            const bool hookSaw = !seen.used && t - seen.at >= 0 && t - seen.at < kHookTwinWindow && (seen.scan == e.scan || e.scan == 0);
            if (hookSaw) seen.used = true;
            post({InputEvent::Kind::rawKey, 0, e.vk, (e.keyDown ? 1 : 0) | (hookSaw ? 0 : 2), t});
            if (!hookSaw) break;
            if (e.keyDown) {
                const bool wasInFlight = chord_.inFlight(t);
                if (fedMake_[slot]) {
                    feedChord(e.vk, true, t);  // a typematic repeat: the decoder ignores it but stays fresh
                    break;
                }
                fedMake_[slot] = true;
                feedChord(e.vk, true, t);
                if (!wasInFlight && chord_.inFlight(t)) {
                    // Windows holds this Win down too (it had to pass for Raw Input to name its device). A key that
                    // means nothing, between its make and its break, keeps the Start menu shut on the release.
                    const InputConfig cfg = config();
                    if (cfg.swallowChords && cfg.enabled) {
                        maskPending_ = true;
                        if (hookDepth_ == 0) flushMask();  // from the loop: at once; from inside a callback: as it returns
                    }
                }
            } else {
                if (!fedMake_[slot]) break;
                fedMake_[slot] = false;
                feedChord(e.vk, false, t);
            }
            break;
        }
        default:
            break;
    }
}

void InputThread::drainRawInput() noexcept {
    // Only from the outermost hook callback. PM_QS_INPUT keeps PeekMessage to the input queue: sent messages, which
    // is how another hook callback would arrive, stay where they are, so nothing here runs re-entrantly.
    if (hookDepth_ != 1) return;
    HWND w = wnd_.load(std::memory_order_relaxed);
    if (!w) return;
    MSG m;
    int budget = kDrainBudget;
    while (budget-- > 0 && PeekMessageW(&m, w, WM_INPUT, WM_INPUT, PM_REMOVE | PM_NOYIELD | PM_QS_INPUT)) {
        handleRawMessage(reinterpret_cast<HRAWINPUT>(m.lParam), false);
        DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
    }
}

// MARK: hook timing

void InputThread::noteHookTime(Seconds start) noexcept {
    raiseMax(maxHookMicros_, static_cast<std::uint32_t>(std::max(0.0, (now() - start) * 1e6)));
}

void InputThread::noteChainTime(Seconds start) noexcept {
    raiseMax(maxChainMicros_, static_cast<std::uint32_t>(std::max(0.0, (now() - start) * 1e6)));
}

void InputThread::noteHookLag(DWORD eventTime) noexcept {
    // The hook structs carry the event's tick count: how long it queued before reaching us says whether this
    // thread (or something ahead of it) ever stalls long enough for Windows to drop the hook.
    const DWORD lag = GetTickCount() - eventTime;
    if (lag < 60000) raiseMax(maxHookLagMs_, lag);
}

// MARK: hooks (system side)

LRESULT CALLBACK InputThread::mouseHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        InputThread& self = *instance_;
        self.mouseCalls_.fetch_add(1, std::memory_order_relaxed);
        ++self.hookDepth_;
        const Seconds t0 = self.now();
        self.lastMouseHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        const bool swallow = self.onMouse(wp, *reinterpret_cast<const MSLLHOOKSTRUCT*>(lp));
        self.noteHookTime(t0);
        self.flushMask();
        --self.hookDepth_;
        if (swallow) return 1;
        const Seconds t1 = self.now();
        const LRESULT r = CallNextHookEx(nullptr, code, wp, lp);  // other programs' hooks run inside this call
        self.noteChainTime(t1);
        self.lastMouseHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);  // alive at the end too: a slow chain is not a dead hook
        return r;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK InputThread::keyHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        InputThread& self = *instance_;
        self.keyCalls_.fetch_add(1, std::memory_order_relaxed);
        ++self.hookDepth_;
        const Seconds t0 = self.now();
        self.lastKeyHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        const bool swallow = self.onKey(wp, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp));
        self.noteHookTime(t0);
        self.flushMask();
        --self.hookDepth_;
        if (swallow) return 1;
        const Seconds t1 = self.now();
        const LRESULT r = CallNextHookEx(nullptr, code, wp, lp);
        self.noteChainTime(t1);
        self.lastKeyHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        return r;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool InputThread::onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept {
    const Seconds t = now();
    if (isOurs(m.dwExtraInfo)) return false;  // our own synthesised event
    noteHookLag(m.time);
    drainRawInput();  // the mouse's reports are usually in already: the hook sees the device without waiting
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
            lastMoveQpc_.store(qpcNow(), std::memory_order_relaxed);
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
                const bool surfaceMoving = cfg.anyMouse || (moverKnown_ && lastMoverSurface_);
                if (hit && cfg.enabled && surfaceMoving) post({InputEvent::Kind::systemEvent, 0, static_cast<int>(HapticEvent::screenEdge), 0, t});
            }
            return false;
        }
        case WM_LBUTTONDOWN: {
            bool surface = false;
            RawEntry probe{RawKind::button, false, 0, 0, false, false, RI_MOUSE_LEFT_BUTTON_DOWN, t};
            const bool matched = injected ? false : matchRaw(probe, t, surface);
            if (!matched && !injected) {
                // Its report is not in yet: this mouse is live and nothing else clicked lately -> ours; the report is retired when it lands.
                surface = cfg.anyMouse || (cfg.shapeWheel && t - lastOtherButton_ > kOtherDeviceQuiet);
                expectRaw(probe);
            }
            leftDownSurface_ = surface;
            if (ringOpen_ && !injected) {
                post({InputEvent::Kind::ringClick, 0, 0, 0, t});
                swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                return true;
            }
            if (surface && !injected && chord_.swallowLeftClick(t)) {
                // The fake click of the double-press chord, or one in its wake: the decoder hears it here, never from Raw Input.
                const ChordOutput out = chord_.mouseLeftDown(t);
                if (out != ChordOutput::none) post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
                if (cfg.swallowChords && cfg.enabled) {
                    swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                    return true;
                }
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
            if (dragDistance_ > kDragThreshold && cfg.enabled && leftDownSurface_) {
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
                // Its report is not in yet: the Mac's rule, while this mouse is live and no other device scrolled
                // recently, the notch is ours. The late report is retired when it lands.
                surface = cfg.anyMouse || (cfg.shapeWheel && t - lastOtherWheel_ > kOtherDeviceQuiet);
                expectRaw(probe);
            }
            if (!surface || !cfg.shapeWheel || !cfg.enabled) return false;
            if (isMenuUnder(m.pt)) return false;  // popup menus and combo lists: one notch, one step, as Windows does it
            const double notches = static_cast<double>(delta) / WHEEL_DELTA;
            if (cfg.kinetic.enabled) {
                const bool coasting = kinetic_.active();
                if (msg == WM_MOUSEWHEEL) kinetic_.notch(notches, 0, t, cfg.kinetic);
                else kinetic_.notch(0, notches, t, cfg.kinetic);
                if (!coasting) {
                    // The frame clock starts with the first notch and keeps its phase through the rest: restarting
                    // it on every notch stretched frames to 16 ms and made a spin stutter.
                    LARGE_INTEGER due;
                    due.QuadPart = -static_cast<LONGLONG>(kCoastFrame * 1e7);
                    SetWaitableTimer(timer_, &due, static_cast<LONG>(kCoastFrame * 1000), nullptr, nullptr, FALSE);
                    lastCoastTick_ = 0;
                }
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
    noteHookLag(k.time);
    const bool injected = (k.flags & LLKHF_INJECTED) != 0;
    drainRawInput();  // the reports of the keys before this one are in: the chord they started is known by now
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
    forgetStaleChord(t);
    const int idx = chordIndex(k.vkCode);
    const auto slot = static_cast<std::size_t>(idx);
    (down ? hookMake_ : hookBreak_)[slot] = HookSeen{t, static_cast<std::uint16_t>(k.scanCode), false};
    bool swallow = false;
    if (!down) {
        // A break follows its make. A letter the hook ate stays hidden to the end, and the decoder hears the
        // break of every letter it heard the make of; a letter Windows holds is released to Windows. Shift and Win
        // breaks pass, and their report feeds the decoder a moment later. A Win release passing while the mask is
        // still pending would open the Start menu: the mask goes first.
        if ((idx == kVkLWin || idx == kVkRWin) && maskPending_) flushMask();
        if (swallowedMake_[slot] && t - swallowedAt_[slot] > kForgetSwallowed) swallowedMake_[slot] = false;  // its break was lost long ago
        swallow = swallowedMake_[slot];
        swallowedMake_[slot] = false;
        letterPassed_[slot] = false;
        if (isLetter(idx) && fedMake_[slot]) {
            fedMake_[slot] = false;
            feedChord(idx, false, t);
        }
    } else if (isLetter(idx)) {
        // S or F23 while the mouse's Win chord is in flight completes it. Hidden from Windows by default (no
        // Snipping Tool, no Copilot), and the decoder hears it from here either way. A letter Windows already holds
        // (the user's own S, typed before the chord) is the user's: it keeps passing, repeats and release alike,
        // and the chord goes without it. Shift and Win makes pass: Raw Input names their device a millisecond
        // later and feeds the decoder then (onRaw).
        if (letterPassed_[slot]) {
            swallow = false;
        } else if (swallowedMake_[slot]) {
            swallow = true;  // a repeat of a make the hook ate: hidden, and it keeps the chord fresh
            feedChord(idx, true, t);
        } else if (chord_.inFlight(t)) {
            swallow = cfg.swallowChords && cfg.enabled;
            swallowedMake_[slot] = swallow;
            swallowedAt_[slot] = t;
            letterPassed_[slot] = !swallow;
            fedMake_[slot] = true;
            feedChord(idx, true, t);
        } else {
            letterPassed_[slot] = true;
        }
    }
    post({InputEvent::Kind::rawKey, static_cast<std::uint8_t>(swallow ? 3 : 2), idx, down ? 1 : 0, t});
    return swallow;
}

// MARK: timers

void InputThread::tick(Seconds t) noexcept {
    if (lastCoastTick_ > 0) raiseMax(maxCoastGapMs_, static_cast<std::uint32_t>((t - lastCoastTick_) * 1000));
    lastCoastTick_ = t;
    const auto step = kinetic_.tick(t);
    if (!step.active) {
        CancelWaitableTimer(timer_);
        wheelOut_.reset();
        lastCoastTick_ = 0;
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
