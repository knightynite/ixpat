#include "win/crash_dump.h"
#include "win/log.h"
#include "win/input_thread.h"

#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>

namespace ixpat::win {
namespace {

constexpr Seconds kSwallowReleaseWindow = 1.5;  // a swallowed press hides its release too, if it comes soon
constexpr Seconds kOtherDeviceQuiet = 2.0;      // another device acted recently: do not claim unmatched events
constexpr Seconds kRawMatchWindow = 0.05;
constexpr Seconds kHookTwinWindow = 0.03;    // a report and its hook callback lie this close (keyboard: hook first; mouse: report first)
constexpr Seconds kHookAnswerWindow = 0.05;       // a keyboard report still without its callback after this: the hook is gone (the callback comes first)
constexpr Seconds kMouseAnswerWindow = 0.12;      // the mouse's callback follows its report, behind whatever hooks sit ahead of ours
constexpr bool kProbesEnabled = true;     // keyboard only: that hook dies between presses with nothing of ours stalling; the mouse hook does not
constexpr Seconds kProbeEvery = 0.25;     // hook probes this often while the user is active
constexpr Seconds kProbeAnswer = 0.15;    // a probe not seen by its hook within this: the hook is dead
constexpr Seconds kProbeActivity = 3.0;   // probes only this soon after physical input (injected input resets the idle timer)
constexpr Seconds kHookRefresh = 2.0;     // passive recovery while idle; no injected keys that reset Windows' idle timer
constexpr double kDragThreshold = 20;
constexpr double kHoldMoveCancel = 4;
constexpr Seconds kCoastFrame = 1.0 / 120;
constexpr int kDrainBudget = 32;
constexpr UINT WM_APP_REINSTALL = WM_APP + 0x40;

bool isChordKey(DWORD vk) { return ChordGate::isChordKey(static_cast<int>(vk & 0xFF)); }

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
    for (;;) {
        const std::size_t i = cfgIndex_.load(std::memory_order_seq_cst);
        cfgReading_.store(i, std::memory_order_seq_cst);
        if (cfgIndex_.load(std::memory_order_seq_cst) != i) continue;  // published again meanwhile: register for the new one
        InputConfig c = cfgSlots_[i];  // the writer never writes the published slot nor the registered one
        cfgReading_.store(kConfigSlots, std::memory_order_seq_cst);
        return c;
    }
}

void InputThread::setConfig(const InputConfig& c) {
    const bool nowDisabled = cfgLast_.enabled && !c.enabled;
    cfgLast_ = c;
    const std::size_t cur = cfgIndex_.load(std::memory_order_seq_cst);
    std::size_t next = (cur + 1) % kConfigSlots;
    if (next == cfgReading_.load(std::memory_order_seq_cst)) next = (next + 1) % kConfigSlots;  // a reader is in that one
    cfgSlots_[next] = c;
    cfgIndex_.store(next, std::memory_order_seq_cst);
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
    s.maskFailures = maskFailures();
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
    startInjectors();
    main_ = mainWindow;
    message_ = message;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!stopEvent_ || !ready_ || !timer_) {  // out of handles: the app runs without the input thread
        debugLog("input thread: could not create its events or timer (error " + std::to_string(GetLastError()) + ")");
        stop();
        return false;
    }
    instance_ = this;
    // The ring is open in toggle mode: the gesture (a press's letter, the double press's click) commits it instead
    // of running its action (the Mac's `button.pressIntercept`). The decoder hides the chord; main closes the ring.
    gate_.decoder().pressIntercept = [this] {
        if (!(ringOpen_ && !ringHold_)) return false;
        post({InputEvent::Kind::ringClick, 0, 0, 0, now()});
        return true;
    };
    thread_ = std::thread([this] {
        ixpat::win::reserveStackForCrashHandler();  // the crash handler needs room here after a stack overflow
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
            installHooks("start");
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

bool InputThread::installHooks(const char* why) {
    // A replacement is already live. Do not reuse its predecessor's callback slot
    // until the message pump has delivered callbacks queued before that replacement.
    if (retiredMouseHook_ || retiredKeyHook_) return true;
    const long long t0 = qpcTicks();
    HINSTANCE self = GetModuleHandleW(nullptr);
    const int nextSlot = 1 - hookSlot_;
    DWORD err = 0;
    // Install first, then retire the old pair. A retired callback already queued by
    // Windows must still run; only a duplicate reached through our own chain is skipped.
    HHOOK nextMouse = SetWindowsHookExW(WH_MOUSE_LL, nextSlot ? &InputThread::mouseHook<1> : &InputThread::mouseHook<0>, self, 0);
    if (!nextMouse) err = GetLastError();
    HHOOK nextKey = SetWindowsHookExW(WH_KEYBOARD_LL, nextSlot ? &InputThread::keyHook<1> : &InputThread::keyHook<0>, self, 0);
    if (!nextKey && !err) err = GetLastError();
    const bool installed = nextMouse && nextKey;
    if (installed) {
        const HHOOK oldMouse = mouseHook_, oldKey = keyHook_;
        mouseHook_ = nextMouse;
        keyHook_ = nextKey;
        hookSlot_ = nextSlot;
        retiredMouseHook_ = oldMouse;
        retiredKeyHook_ = oldKey;
        retireHooksAt_ = now() + 0.1;
    } else {
        if (nextMouse) UnhookWindowsHookEx(nextMouse);
        if (nextKey) UnhookWindowsHookEx(nextKey);
    }
    if (const double ms = msSince(t0); ms >= 20) post({InputEvent::Kind::slowPath, 3, static_cast<int>(ms), 0, now()});
    mouseOk_ = mouseHook_ != nullptr;
    keyOk_ = keyHook_ != nullptr;
    installError_.store(err, std::memory_order_relaxed);
    rawMouseUnanswered_ = rawKeyUnanswered_ = 0;
    // Observations and held keys remain valid across the handover. In particular, a
    // mouse modifier queued before the replacement still belongs to the next letter.
    lastHookRefresh_ = now();
    probeQueuedAt_ = 0;  // a probe in flight was for the hooks just removed
    probeJudgedSentQpc_ = probeSentQpc();
    lastInstallAt_ = now();
    lastInstallWhy_ = why;
    if (std::strcmp(why, "refresh") != 0 || !installed) {
        char line[160];
        std::snprintf(line, sizeof line, "hooks installed (%s)%s", why, installed ? "" : " FAILED");
        debugLog(line);
    }
    return installed;
}

void InputThread::retireHooks() noexcept {
    if (retiredMouseHook_) UnhookWindowsHookEx(retiredMouseHook_);
    if (retiredKeyHook_) UnhookWindowsHookEx(retiredKeyHook_);
    retiredMouseHook_ = retiredKeyHook_ = nullptr;
    retireHooksAt_ = 0;
}

void InputThread::removeHooks() {
    retireHooks();
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
            // Requests from main wait for a quiet keyboard; urgent loss detection
            // can replace the hooks mid-chord without clearing the gate's state.
            instance_->rehookAt_ = instance_->now();
            instance_->reinstallAsked_ = true;
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
        if (retireHooksAt_ > 0) timeoutMs = std::min<DWORD>(timeoutMs, 50);
        if (rawMouseUnanswered_ > 0 || rawKeyUnanswered_ > 0) timeoutMs = std::min<DWORD>(timeoutMs, 50);  // a report awaits its hook callback
        if (t - static_cast<double>(lastPhysicalQpc_.load(std::memory_order_relaxed)) / qpcFreq_ < kProbeActivity) timeoutMs = std::min<DWORD>(timeoutMs, 50);  // probes due
        if (rehookAt_ > 0) timeoutMs = std::min(timeoutMs, static_cast<DWORD>(std::max(0.0, std::min(1.0, rehookAt_ - t) * 1000)));
        if (t >= nextMonitorRefresh_) {
            refreshMonitors();
            nextMonitorRefresh_ = t + 5;
        }
        const DWORD r = MsgWaitForMultipleObjectsEx(2, waits, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) break;
        const long long awake = qpcTicks();
        int served = 0;
        if (r == WAIT_OBJECT_0 + 1) tick(now());  // coast frame
        while (served < 64 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {  // bounded: a burst must not starve the checks below
            if (m.message == WM_QUIT) return;
            TranslateMessage(&m);
            DispatchMessageW(&m);
            ++served;
            if (rehookNow_ && hookDepth_ == 0) {  // a chord key the hook missed: back before the next message, not after the burst
                rehookNow_ = false;
                installHooks(rehookWhy_);
                reinstalls_.fetch_add(1, std::memory_order_relaxed);
                post({InputEvent::Kind::hooksReinstalled, 0, 8, static_cast<int>(installError_.load(std::memory_order_relaxed)), now()});
            }
        }
        if (const double ms = msSince(awake); ms >= 50) post({InputEvent::Kind::slowPath, 2, static_cast<int>(ms), served, now()});
        // Unhooking immediately after replacement cancels callbacks already queued
        // for the old handle. Let the pump drain those first; new events see the new
        // hook, and chaining to the retired one is deduplicated by the wrappers.
        if (retireHooksAt_ > 0 && now() >= retireHooksAt_) retireHooks();
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
        if (disableRequested_.exchange(false)) applyDisable();
        if (anyMouseRequested_.load() != anyMouse_) {
            anyMouse_ = anyMouseRequested_.load();
            raw_.setAnyMouse(anyMouse_);
        }
        if (holdArmed_ && now() >= holdDeadline_) fireHoldAction(now());
        flushMask();
        checkHookLiveness();
        probeHooks(now());
        // While nothing is in flight, put the hooks back every few seconds: Windows drops them silently and tells
        // nobody, and a dropped hook must not wait for the next key to be found out.
        const Seconds t2 = now();
        forgetStaleChord(t2);  // a chord whose breaks never came goes quiet here too, not only on the next key
        reconcileLetters();
        const Seconds sinceKey = t2 - static_cast<double>(lastPhysKeyHookQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
        const Seconds sincePhysical = t2 - static_cast<double>(lastPhysicalQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
        const bool due = (sincePhysical > kProbeActivity && t2 - lastHookRefresh_ >= kHookRefresh) || (rehookAt_ > 0 && t2 >= rehookAt_);
        if (due && sinceKey > 0.2 && gate_.idle(t2) && !leftDown_ && !holdArmed_ && !gestureActive_) {
            installHooks(reinstallAsked_ ? "asked by main" : rehookAt_ > 0 ? "device change" : "refresh");
            rehookAt_ = 0;
            if (reinstallAsked_) {
                reinstallAsked_ = false;
                reinstalls_.fetch_add(1, std::memory_order_relaxed);
                post({InputEvent::Kind::hooksReinstalled, 0, 0, static_cast<int>(installError_.load(std::memory_order_relaxed)), t2});
            }
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
    const long long t0 = qpcTicks();
    monitorCount_ = 0;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT r, LPARAM p) -> BOOL {
            auto* self = reinterpret_cast<InputThread*>(p);
            if (self->monitorCount_ < static_cast<int>(self->monitors_.size())) self->monitors_[self->monitorCount_++] = *r;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    if (const double ms = msSince(t0); ms >= 20) post({InputEvent::Kind::slowPath, 4, static_cast<int>(ms), 0, now()});
}

// MARK: the chord gate (the decisions live in core/chord_gate, replayed in tests; this is the plumbing)

void InputThread::postChord(ChordOutput out, Seconds t) noexcept {
    if (out == ChordOutput::none) return;
    // A hold ends on the input thread at once, so moves after the physical release never feed the
    // accumulator (main's gesturesEnd() follows and is idempotent).
    if (out == ChordOutput::holdEnd) endGestureNow();
    post({InputEvent::Kind::chord, 0, static_cast<int>(out), 0, t});
}

void InputThread::forgetStaleChord(Seconds t) noexcept { postChord(gate_.tick(t), t); }

void InputThread::reconcileLetters() noexcept {
    // Windows' own view of the letters settles what the bookkeeping could not see while a hook was away.
    gate_.reconcileLetters((GetAsyncKeyState(kVkS) & 0x8000) != 0, (GetAsyncKeyState(kVkF23) & 0x8000) != 0);
}

void InputThread::flushMask() noexcept {
    if (!maskPending_) return;
    maskPending_ = false;
    maskWanted_ = queueMask();  // the key thread sends it; this thread never waits on SendInput
}

// MARK: hook liveness

void InputThread::noteRawForLiveness(bool mouse, Seconds at, int vk, bool keyDown, bool fromMouse) noexcept {
    // Every physical report has a hook callback within a few microseconds of it: for the mouse the report is posted
    // first and the callback follows, for the keyboard the callback runs first and the report is generated after it
    // returns. A report with no physical callback near it starts a clock; checkHookLiveness() reads it. Our own
    // tagged events (probes, masks) stamp the other counters and vouch for nothing here.
    const auto& lastHook = mouse ? lastPhysMouseHookQpc_ : lastPhysKeyHookQpc_;
    const bool unseen = static_cast<double>(lastHook.load(std::memory_order_relaxed)) / qpcFreq_ < at - kHookTwinWindow;
    if (unseen && !mouse && isChordKey(static_cast<DWORD>(vk))) {
        // A chord key the keyboard hook did not see: Windows has it already and the letter may follow within
        // milliseconds. The hooks go back at once, not after the answer window. A Win make of the mouse's own chord
        // seen only this way gets its mask (its break would open the Start menu); the user's keyboard keeps its Win.
        const int idx = ChordGate::chordIndex(vk);
        if (keyDown && fromMouse && (idx == kVkLWin || idx == kVkRWin)) {
            const InputConfig cfg = config();
            if (cfg.enabled && cfg.swallowChords) {
                maskPending_ = true;
                flushMask();
            }
        }
        rehookNow_ = true;
        rehookWhy_ = "chord key unanswered";
        return;
    }
    Seconds& unanswered = mouse ? rawMouseUnanswered_ : rawKeyUnanswered_;
    if (unanswered > 0) return;
    if (unseen) {
        unanswered = at;
        if (mouse) std::snprintf(unansweredWhat_, sizeof unansweredWhat_, "vk %d down %d", vk, keyDown ? 1 : 0);
    }
}

void InputThread::probeHooks(Seconds t) noexcept {
    if (!kProbesEnabled) return;
    if (probeQueuedAt_ > 0) {
        // The probe counts from the moment the key thread sent it, not from the moment it was queued: a slow send
        // ahead of it is not a dead hook. One that never went out within a second is forgotten.
        const long long sent = probeSentQpc();
        if (sent <= probeJudgedSentQpc_) {
            if (t - probeQueuedAt_ > 1.0) probeQueuedAt_ = 0;
            return;
        }
        if (t - static_cast<double>(sent) / qpcFreq_ <= kProbeAnswer) return;  // the hooks' window is still open
        probeJudgedSentQpc_ = sent;
        probeQueuedAt_ = 0;
        nextProbe_ = t + kProbeEvery;
        if (!probeSendOk()) {  // Windows refused the injection (an elevated window has the foreground?): no verdict
            nextProbe_ = t + 2.0;
            return;
        }
        const bool answered = probeKeySeenQpc_.load(std::memory_order_acquire) >= sent;
        if (keyHealth_.observe(answered, t)) {
            installHooks("keyboard probe unanswered");
            reinstalls_.fetch_add(1, std::memory_order_relaxed);
            post({InputEvent::Kind::hooksReinstalled, 0, 6, static_cast<int>(installError_.load(std::memory_order_relaxed)), t});
        }
        return;
    }
    if (t < nextProbe_) return;
    const Seconds sincePhysical = t - static_cast<double>(lastPhysicalQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
    const Seconds sinceKey = t - static_cast<double>(lastKeyHookQpc_.load(std::memory_order_relaxed)) / qpcFreq_;
    if (sincePhysical > kProbeActivity || sinceKey < 0.1 || !gate_.idle(t) || gestureActive_ || holdArmed_ || leftDown_ || ringOpen_) {
        nextProbe_ = t + 0.05;  // not now: look again shortly
        return;
    }
    probeJudgedSentQpc_ = probeSentQpc();  // sends before this one are not this probe
    queueHookProbe();
    probeQueuedAt_ = t;
    nextProbe_ = t + kProbeEvery;
}

void InputThread::checkHookLiveness() noexcept {
    const Seconds t = now();
    auto dead = [&](Seconds& unanswered, const std::atomic<std::int64_t>& lastHook, Seconds window) {
        if (unanswered <= 0) return false;
        if (static_cast<double>(lastHook.load(std::memory_order_relaxed)) / qpcFreq_ >= unanswered - kHookTwinWindow) {
            unanswered = 0;  // its callback came (the mouse hook's a moment after the report)
            return false;
        }
        if (t - unanswered < window) return false;
        unanswered = 0;
        return true;
    };
    bool mouseDead = dead(rawMouseUnanswered_, lastPhysMouseHookQpc_, kMouseAnswerWindow);
    const bool keyDead = dead(rawKeyUnanswered_, lastPhysKeyHookQpc_, kHookAnswerWindow);
    if (mouseDead) {
        // A move report whose motion left the pointer where the last callback saw it produced no event Windows
        // would hook (a slow move under the acceleration curve, a move while the pointer is held): no verdict.
        POINT p{};
        const std::int64_t last = lastHookPt_.load(std::memory_order_relaxed);
        if (GetCursorPos(&p) && p.x == static_cast<LONG>(last >> 32) && p.y == static_cast<LONG>(static_cast<std::int32_t>(last & 0xFFFFFFFF))) mouseDead = false;
    }
    if (!mouseDead && !keyDead) return;
    // Windows removed a hook (it does so silently when a callback, ours or one further down the chain, stalls).
    {
        char line[200];
        const double sinceM = (t - static_cast<double>(lastMouseHookQpc_.load(std::memory_order_relaxed)) / qpcFreq_) * 1000;
        const double sinceK = (t - static_cast<double>(lastKeyHookQpc_.load(std::memory_order_relaxed)) / qpcFreq_) * 1000;
        std::snprintf(line, sizeof line, "hook dead (%s%s%s): last mouse callback %.0f ms ago, last keyboard callback %.0f ms ago, last install %.0f ms ago (%s); first unanswered: %s",
                      mouseDead ? "mouse" : "", mouseDead && keyDead ? " and " : "", keyDead ? "keyboard" : "", sinceM, sinceK, (t - lastInstallAt_) * 1000, lastInstallWhy_, unansweredWhat_);
        debugLog(line);
    }
    installHooks(mouseDead && keyDead ? "mouse and keyboard reports unanswered" : mouseDead ? "mouse reports unanswered" : "keyboard reports unanswered");
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
        lastPhysicalQpc_.store(e.qpc, std::memory_order_relaxed);
        // Only a report that Windows turns into an input event has a hook callback to answer it: a move of zero, a
        // button report with no change, a wheel of zero, a consumer or vendor report, an overrun key do not.
        const bool makesEvent = (e.kind == RawKind::move && (e.dx || e.dy)) || (e.kind == RawKind::button && e.buttonFlags) ||
                                ((e.kind == RawKind::wheel || e.kind == RawKind::hwheel) && e.wheel) || (e.kind == RawKind::key && e.vk != 0 && e.vk != 0xFF);
        if (makesEvent) {
            const int shape = e.kind == RawKind::key ? e.vk : e.kind == RawKind::move ? 1000 + std::abs(e.dx) + std::abs(e.dy) : e.kind == RawKind::button ? 2000 + e.buttonFlags : 3000 + e.wheel;
            noteRawForLiveness(e.kind != RawKind::key, t, shape, e.kind == RawKind::key ? e.keyDown : e.surfaceMouse, e.surfaceMouse);
        }
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
            if (!isChordKey(e.vk) || e.injected) break;
            // Device reports identify the mouse's modifiers, even during hook recovery.
            // Only the letter's hook callback may start an action: a letter first seen
            // through Raw Input has already reached Windows.
            const InputConfig cfg = config();
            const ChordGate::Decision d = gate_.rawKey(e.vk, e.keyDown, e.scan, e.surfaceMouse, t, {cfg.swallowChords, cfg.enabled});
            // The gate must retire other keyboards' hook observations, but key identities never leave this thread.
            if (!e.surfaceMouse) break;
            postChord(d.out, t);
            if (d.sendMask) {
                // Windows holds this Win down too (it had to pass for Raw Input to name its device). A key that
                // means nothing, between its make and its break, keeps the Start menu shut on the release.
                maskPending_ = true;
                flushMask();  // queued for the key thread at once: it is normally out long before the release
            }
            break;
        }
        default:
            break;
    }
}

void InputThread::drainRawInput(int budget) noexcept {
    // Only from the outermost hook callback. PM_QS_INPUT keeps PeekMessage to the input queue: sent messages, which
    // is how another hook callback would arrive, stay where they are, so nothing here runs re-entrantly.
    if (hookDepth_ != 1) return;
    HWND w = wnd_.load(std::memory_order_relaxed);
    if (!w) return;
    const Seconds deadline = now() + 0.002;  // a report burst must never hold up a system hook
    MSG m;
    while (budget-- > 0 && PeekMessageW(&m, w, WM_INPUT, WM_INPUT, PM_REMOVE | PM_NOYIELD | PM_QS_INPUT)) {
        handleRawMessage(reinterpret_cast<HRAWINPUT>(m.lParam), false);
        DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
        if (now() >= deadline) break;
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
    // The struct's time is the 32-bit tick count: the low half of the 64-bit one, so the difference wraps correctly.
    const DWORD lag = static_cast<DWORD>(GetTickCount64()) - eventTime;
    if (lag < 60000) raiseMax(maxHookLagMs_, lag);
    if (lag >= 100 && lag < 60000) post({InputEvent::Kind::slowPath, 1, static_cast<int>(lag), 0, now()});
}

// MARK: hooks (system side)

template<int Slot>
LRESULT CALLBACK InputThread::mouseHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        InputThread& self = *instance_;
        const auto& event = *reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        const auto* f = self.forwardingMouse_;
        const WPARAM forwardedMessage = self.forwardingMouseMessage_;
        if (Slot != self.hookSlot_ && f && forwardedMessage == wp && f->time == event.time && f->pt.x == event.pt.x && f->pt.y == event.pt.y &&
            f->mouseData == event.mouseData && f->flags == event.flags && f->dwExtraInfo == event.dwExtraInfo)
            return CallNextHookEx(nullptr, code, wp, lp);
        self.mouseCalls_.fetch_add(1, std::memory_order_relaxed);
        ++self.hookDepth_;
        const Seconds t0 = self.now();
        self.lastMouseHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        const bool swallow = self.onMouse(wp, *reinterpret_cast<const MSLLHOOKSTRUCT*>(lp));
        self.flushMask();
        self.noteHookTime(t0);
        --self.hookDepth_;
        if (swallow) return 1;
        const Seconds t1 = self.now();
        self.forwardingMouse_ = &event;
        self.forwardingMouseMessage_ = wp;
        const LRESULT r = CallNextHookEx(nullptr, code, wp, lp);  // other programs' hooks run inside this call
        self.forwardingMouse_ = f;
        self.forwardingMouseMessage_ = forwardedMessage;
        self.noteChainTime(t1);
        self.lastMouseHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);  // alive at the end too: a slow chain is not a dead hook
        return r;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

template<int Slot>
LRESULT CALLBACK InputThread::keyHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION && instance_) {
        InputThread& self = *instance_;
        const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const auto* f = self.forwardingKey_;
        const WPARAM forwardedMessage = self.forwardingKeyMessage_;
        if (Slot != self.hookSlot_ && f && forwardedMessage == wp && f->time == event.time && f->vkCode == event.vkCode &&
            f->scanCode == event.scanCode && f->flags == event.flags && f->dwExtraInfo == event.dwExtraInfo)
            return CallNextHookEx(nullptr, code, wp, lp);
        self.keyCalls_.fetch_add(1, std::memory_order_relaxed);
        ++self.hookDepth_;
        const Seconds t0 = self.now();
        self.lastKeyHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        const bool swallow = self.onKey(wp, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp));
        self.flushMask();
        self.noteHookTime(t0);
        --self.hookDepth_;
        if (swallow) return 1;
        const Seconds t1 = self.now();
        self.forwardingKey_ = &event;
        self.forwardingKeyMessage_ = wp;
        const LRESULT r = CallNextHookEx(nullptr, code, wp, lp);
        self.forwardingKey_ = f;
        self.forwardingKeyMessage_ = forwardedMessage;
        self.noteChainTime(t1);
        self.lastKeyHookQpc_.store(self.qpcNow(), std::memory_order_relaxed);
        return r;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool InputThread::onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept {
    const Seconds t = now();
    if (m.dwExtraInfo == kProbeTag) {  // a probe: noted, and eaten so no one else sees it
        return true;
    }
    if (isOurs(m.dwExtraInfo)) return false;  // our own synthesised event
    lastPhysMouseHookQpc_.store(qpcNow(), std::memory_order_relaxed);
    lastHookPt_.store((static_cast<std::int64_t>(m.pt.x) << 32) | static_cast<std::uint32_t>(m.pt.y), std::memory_order_relaxed);
    noteHookLag(m.time);
    drainRawInput(kDrainBudget);  // the mouse's reports are usually in already: the hook sees the device without waiting
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
            if (surface && !injected) {
                // The fake click of the double-press chord, or one in its wake: the decoder hears it here, never from
                // Raw Input, and before the ring does, so the chord closes properly. With the ring open the click is
                // the ring's commit (the decoder asks it through pressIntercept; a late click is handed over here).
                const bool ringWasOpen = ringOpen_;
                const ChordGate::Decision d = gate_.leftDown(t, {cfg.swallowChords, cfg.enabled}, ringWasOpen);
                if (d.out != ChordOutput::none) {
                    if (ringWasOpen) post({InputEvent::Kind::ringClick, 0, 0, 0, t});
                    else postChord(d.out, t);
                }
                if (d.swallow) {
                    swallowLeftUpUntil_ = t + kSwallowReleaseWindow;
                    return true;
                }
            }
            if (ringOpen_ && !injected) {
                post({InputEvent::Kind::ringClick, 0, 0, 0, t});
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
            if (!injectorReady()) return false;  // nothing could be sent back: the physical notch is better than none
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
    if (k.dwExtraInfo == kProbeTag) {  // a probe: noted, and eaten so no one else sees it
        probeKeySeenQpc_.store(qpcNow(), std::memory_order_release);
        return true;
    }
    if (isOurs(k.dwExtraInfo)) return false;
    lastPhysKeyHookQpc_.store(qpcNow(), std::memory_order_relaxed);
    noteHookLag(k.time);
    const bool injected = (k.flags & LLKHF_INJECTED) != 0;
    // The identifying modifier may sit behind a burst of pointer reports. Give chord
    // keys the larger bounded drain before deciding whether their shortcut may pass.
    drainRawInput(isChordKey(k.vkCode) ? 1024 : kDrainBudget);
    const Seconds t = now();
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
    const int idx = ChordGate::chordIndex(static_cast<int>(k.vkCode));
    bool maskLate = false;
    if (!down && (idx == kVkLWin || idx == kVkRWin)) {
        // The Win make's report may still sit behind a burst of mouse reports; it is what arms the chord and asks
        // for the mask, and the mask must be out before this release reaches Windows.
        drainRawInput(1024);
        flushMask();
        maskLate = maskWanted_ > maskSentSequence();
    }
    const ChordGate::Decision d = gate_.hookKey(static_cast<int>(k.vkCode), down, static_cast<std::uint16_t>(k.scanCode), t, {cfg.swallowChords, cfg.enabled});
    postChord(d.out, t);
    bool swallow = d.swallow;
    if (maskLate && !d.swallow && injectorReady()) {
        // The mask has not left yet (it normally left at the make, some 20 ms ago). This thread must not wait on it,
        // so the physical release is swallowed and a tagged one is queued behind the mask on the key thread. Without
        // a key thread the release passes: a Start menu once is better than a Win key that stays down.
        queueWinRelease(static_cast<WORD>(k.vkCode), static_cast<WORD>(k.scanCode));
        swallow = true;
        // Windows generates no report for a key the hook ate, and the gate feeds the decoder from reports: hand it
        // the one this release will never have, so the chord ends as it would have.
        postChord(gate_.rawKey(static_cast<int>(k.vkCode), false, static_cast<std::uint16_t>(k.scanCode), true, t, {cfg.swallowChords, cfg.enabled}).out, t);
    }
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
