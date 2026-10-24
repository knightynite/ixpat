#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <thread>

#include "core/chord.h"
#include "core/clock.h"
#include "core/events.h"
#include "core/gesture.h"
#include "core/kinetic.h"
#include "core/settings.h"
#include "win/raw_input.h"
#include "win/send_input.h"

namespace ixpat::win {

/// Settings the input thread needs, pushed from main as one snapshot (the Mac's pushConfig).
/// Plain data only: copying it inside a hook must never allocate.
struct InputConfig {
    bool enabled = true;
    KineticConfig kinetic;          // enabled = smooth scrolling is on and the mouse is live
    bool reverseOnly = false;       // reverse without smoothing
    bool shapeWheel = false;        // the Surface Mouse is connected (or recently detached) and enabled
    bool swallowChords = true;      // hide the action button's chords from apps
    bool holdActionEnabled = false;
    int holdActionMs = 350;
    HoldAction holdAction = HoldAction::contextMenu;
    GestureConfig gesture;
    bool anyMouse = false;          // IXPAT_ANY_MOUSE
};

/// What the input thread tells the main thread. Posted as `message` after pushing into a ring buffer.
struct InputEvent {
    enum class Kind : std::uint8_t {
        rawButtons,    // a = old mask, b = new mask (Surface Mouse)
        rawWheel,      // b = notches (signed)
        rawKey,        // for the report log: a = 0 the mouse's keyboard (Raw Input), 1 another device, 2 hook passed, 3 hook swallowed;
                       // b = vk; c = bit 0 down, bit 1 the hook never saw it
        chord,         // b = ChordOutput
        systemEvent,   // b = HapticEvent (screenEdge, dragEnd, forceClick)
        gestureAction, // b = direction (GestureDir), c = gesture session id
        ringClick,     // left click while the ring was open: commit
        ringCancel,    // right click / Escape while the ring was open
        ringOpenRequest,  // hold action = Action Ring
        dragEnded,     // b = x, c = y of the release (snap check follows on main)
        pointerOwner,  // b = 1 the Surface Mouse moved the pointer last, 0 another device did
        overflow,      // the ring buffer overflowed: main re-synchronises gesture / hold state
        hooksReinstalled,  // b = why (0 asked by main, bit 0 mouse reports unanswered, bit 1 keyboard), c = SetWindowsHookEx error (0 = none)
    };
    Kind kind;
    std::uint8_t a = 0;
    std::int32_t b = 0, c = 0;
    Seconds now = 0;
};

/// Status main can read at any time.
struct InputStatus {
    bool rawRegistered = false;
    bool mouseHook = false, keyboardHook = false;
    std::uint32_t mouseHookCalls = 0, keyboardHookCalls = 0;
    std::uint32_t rawMouse = 0, rawKeyboard = 0;
    std::uint32_t maxHookMicros = 0;    // the slowest callback of ours (Windows drops a hook that stalls)
    std::uint32_t maxChainMicros = 0;   // the slowest pass down the chain: other programs' hooks run inside our call
    std::uint32_t maxHookLagMs = 0;     // the longest an event waited between the system and our callback
    std::uint32_t hookReinstalls = 0;   // times the hooks had to be put back
    std::uint32_t hookInstallError = 0; // the last SetWindowsHookEx error (0 = none)
    std::uint32_t maxCoastGapMs = 0;    // the longest gap between two smooth-scroll frames
    std::uint32_t maskFailures = 0;     // times the Start-menu mask key could not be sent
};

/// Owns the low-level hooks, Raw Input, the chord decoder, the kinetic coast timer and gesture mode,
/// all on one TIME_CRITICAL thread. Decides pass / block / replace; never does I/O, never waits.
///
/// Who hears what: Windows runs the keyboard hook before it generates a key's Raw Input (and generates
/// none for a key the hook ate); for the mouse the report is posted just before the hook runs. So the
/// hooks decide alone, at once, and Raw Input names the device a moment later. The mouse's Shift and Win
/// pass (the Win release is kept from opening Start by a mask key), its letters are swallowed once its
/// Win chord is in flight, and a report the hooks never saw is the signal that Windows dropped a hook:
/// nothing fires, the hooks go back.
class InputThread {
public:
    InputThread();
    ~InputThread();
    InputThread(const InputThread&) = delete;
    InputThread& operator=(const InputThread&) = delete;

    bool start(HWND mainWindow, UINT message);
    void stop();
    void setConfig(const InputConfig& c);
    void setRingState(bool open, bool holdMode);
    void gesturesBegin();
    void gesturesEnd();
    /// Main drains the ring buffer after receiving `message`.
    bool pop(InputEvent& out);

    InputStatus status() const;
    Seconds lastPointerMove() const;
    bool leftButtonDown() const { return leftDown_.load(); }
    void reinstallHooks();  // asks the thread to unhook and hook again

private:
    static LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK keyHook(int code, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void loop();
    bool installHooks();
    void removeHooks();
    bool onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept;  // true = swallow
    bool onKey(WPARAM msg, const KBDLLHOOKSTRUCT& k) noexcept;
    void drainRawInput() noexcept;
    void handleRawMessage(HRAWINPUT h, bool allowResolve) noexcept;
    void onRaw(const RawEvent& e) noexcept;
    void post(InputEvent e) noexcept;
    void tick(Seconds now) noexcept;
    void fireHoldAction(Seconds now) noexcept;
    void applyDisable() noexcept;
    void endGestureNow() noexcept;
    void refreshMonitors() noexcept;
    Seconds now() const noexcept;
    std::int64_t qpcNow() const noexcept;
    void feedChord(int vk, bool down, Seconds now) noexcept;
    void forgetStaleChord(Seconds now) noexcept;
    void flushMask() noexcept;
    void noteHookTime(Seconds start) noexcept;
    void noteChainTime(Seconds start) noexcept;
    void noteHookLag(DWORD eventTime) noexcept;
    void noteRawForLiveness(bool mouse, Seconds at) noexcept;
    void checkHookLiveness() noexcept;
    InputConfig config() const noexcept;

    // Attribution: recent raw entries the hooks match on content, and hook events that had to decide
    // before their raw entry arrived (retired when it does, so it cannot pair with a later event).
    struct RawEntry {
        RawKind kind = RawKind::other;
        bool surface = false;
        std::int16_t wheel = 0;
        std::uint16_t scan = 0;
        bool keyDown = false, keyE0 = false;
        std::uint16_t buttonFlags = 0;
        Seconds at = 0;
    };
    bool matchRaw(const RawEntry& probe, Seconds now, bool& surface) noexcept;
    void expectRaw(const RawEntry& probe) noexcept;
    bool retireExpected(const RawEntry& raw) noexcept;

    static InputThread* instance_;

    HWND main_ = nullptr;
    UINT message_ = 0;
    std::atomic<HWND> wnd_{nullptr};
    HHOOK mouseHook_ = nullptr, keyHook_ = nullptr;
    HANDLE stopEvent_ = nullptr, timer_ = nullptr, ready_ = nullptr;
    std::thread thread_;
    double qpcFreq_ = 1;

    // Config snapshots: main writes the next slot and publishes its index; a hook copies the published slot
    // without ever waiting on main. Sixteen slots outlast any reader a writer could lap.
    static constexpr std::size_t kConfigSlots = 16;
    std::array<InputConfig, kConfigSlots> cfgSlots_{};
    std::atomic<std::size_t> cfgIndex_{0};
    InputConfig cfgLast_;  // the writer's own copy, for change detection
    std::atomic<bool> ringOpen_{false};
    std::atomic<bool> ringHold_{false};
    std::atomic<bool> gestureRequested_{false};
    std::atomic<bool> disableRequested_{false};
    std::atomic<bool> anyMouseRequested_{false};

    // Ring buffer to main (SPSC: this thread writes, main reads).
    static constexpr std::size_t kRing = 4096;
    std::array<InputEvent, kRing> ring_{};
    std::atomic<std::size_t> head_{0}, tail_{0};
    std::atomic<bool> posted_{false};
    std::atomic<bool> overflow_{false};

    // Thread-only state
    RawInput raw_;
    ChordDecoder chord_;
    Kinetic kinetic_;
    WheelEmitter wheelOut_;
    GestureAccumulator gestures_;
    bool gestureActive_ = false;
    int gestureSession_ = 0;
    POINT gestureAnchor_{};
    std::uint8_t surfaceButtons_ = 0;
    bool lastMoverSurface_ = false, moverKnown_ = false;
    bool anyMouse_ = false;  // the raw decoder's current mode (mirrors anyMouseRequested_)
    Seconds lastSurfaceWheel_ = -1e9, lastOtherWheel_ = -1e9, lastOtherButton_ = -1e9;
    Seconds swallowLeftUpUntil_ = 0, swallowRightUpUntil_ = 0;
    bool leftDownSurface_ = false;  // the press in progress came from the Surface Mouse
    // What the keyboard hook saw of each chord key, by chordIndex: one observation, consumed by the one report it
    // belongs to (matched by direction, scan code and a short window), so a report the hook never saw is never
    // vouched for by an older callback.
    struct HookSeen {
        Seconds at = -1e9;
        std::uint16_t scan = 0;
        bool used = true;
    };
    std::array<HookSeen, 256> hookMake_{}, hookBreak_{};
    std::bitset<256> swallowedMake_;  // the hook ate this make: its break is eaten too
    std::bitset<256> fedMake_;        // the decoder heard this make: it hears the break
    std::bitset<256> letterPassed_;   // Windows holds this letter from a make the hook passed: nothing of it is ever eaten
    std::array<Seconds, 256> swallowedAt_{};
    bool maskPending_ = false;        // send the Start-menu mask key once the hook callback in progress returns
    POINT dragStart_{};
    double dragDistance_ = 0;
    bool atEdge_ = false;
    bool holdArmed_ = false, holdFired_ = false;
    POINT holdOrigin_{};
    Seconds holdDeadline_ = 0;
    std::array<RECT, 16> monitors_{};
    int monitorCount_ = 0;
    Seconds nextMonitorRefresh_ = 0;
    Seconds lastHookRefresh_ = 0;
    int hookDepth_ = 0;
    std::array<RawEntry, 32> fifo_{};
    std::size_t fifoHead_ = 0, fifoCount_ = 0;
    std::array<RawEntry, 8> expected_{};
    std::size_t expectedNext_ = 0;
    Seconds lastCoastTick_ = 0;
    // Liveness: the oldest physical report of each kind still waiting for a hook callback near it (0 = none).
    Seconds rawMouseUnanswered_ = 0, rawKeyUnanswered_ = 0;

    std::atomic<std::uint32_t> mouseCalls_{0}, keyCalls_{0}, rawMouse_{0}, rawKeyboard_{0};
    std::atomic<bool> rawOk_{false}, mouseOk_{false}, keyOk_{false};
    std::atomic<bool> leftDown_{false};
    std::atomic<std::int64_t> lastMoveQpc_{0};         // any pointing device (the hook)
    std::atomic<std::int64_t> lastSurfaceMoveQpc_{0};  // the Surface Mouse (Raw Input)
    std::atomic<std::int64_t> lastMouseHookQpc_{0}, lastKeyHookQpc_{0};
    std::atomic<std::uint32_t> maxHookMicros_{0}, maxChainMicros_{0}, maxHookLagMs_{0}, maxCoastGapMs_{0};
    std::atomic<std::uint32_t> reinstalls_{0}, installError_{0}, maskFailures_{0};
};

}  // namespace ixpat::win
