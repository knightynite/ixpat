#pragma once
#include <windows.h>

#include <array>
#include <atomic>
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
        rawKey,        // b = vk, c = down (Surface Mouse keyboard collection), for the report log
        chord,         // b = ChordOutput
        systemEvent,   // b = HapticEvent (screenEdge, dragEnd, forceClick)
        gestureAction, // b = 0 up, 1 down, 2 left, 3 right
        ringClick,     // left click while the ring was open: commit
        ringCancel,    // right click while the ring was open
        ringOpenRequest,  // hold action = Action Ring
        dragEnded,     // b = x, c = y of the release (snap check follows on main)
        hookLost,      // Windows dropped a hook; main re-installs via reinstall()
    };
    Kind kind;
    std::uint8_t a = 0;
    std::int32_t b = 0, c = 0;
    Seconds now = 0;
};

/// Owns the low-level hooks, Raw Input, the chord decoder, the kinetic coast timer and gesture mode,
/// all on one TIME_CRITICAL thread. Decides pass / block / replace; never does I/O.
class InputThread {
public:
    InputThread();
    ~InputThread();
    InputThread(const InputThread&) = delete;
    InputThread& operator=(const InputThread&) = delete;

    bool start(HWND mainWindow, UINT message);
    void stop();
    void setConfig(const InputConfig& c);
    void setRingOpen(bool open);
    void gesturesBegin();
    void gesturesEnd();
    /// Main drains the ring buffer after receiving `message`.
    bool pop(InputEvent& out);

    std::uint32_t heartbeat() const { return heartbeat_.load(std::memory_order_relaxed); }  // hook callbacks
    std::uint32_t rawCount() const { return rawCount_.load(std::memory_order_relaxed); }    // WM_INPUT handled
    bool hooksInstalled() const { return hooksOk_.load(); }
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
    bool onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept;   // true = swallow
    bool onKey(WPARAM msg, const KBDLLHOOKSTRUCT& k) noexcept;
    void drainRawInput() noexcept;
    void onRaw(const RawEvent& e) noexcept;
    void post(InputEvent e) noexcept;
    void tick(Seconds now) noexcept;
    void fireHoldAction(Seconds now) noexcept;
    void refreshMonitors() noexcept;
    Seconds now() const noexcept;
    InputConfig config() const noexcept;

    static InputThread* instance_;

    HWND main_ = nullptr;
    UINT message_ = 0;
    HWND wnd_ = nullptr;
    HHOOK mouseHook_ = nullptr, keyHook_ = nullptr;
    HANDLE stopEvent_ = nullptr, timer_ = nullptr;
    std::thread thread_;
    double qpcFreq_ = 1;

    // Config snapshot (main writes, thread reads) under a lock; a tiny struct, copied per event.
    mutable SRWLOCK cfgLock_ = SRWLOCK_INIT;
    InputConfig cfg_;
    std::atomic<bool> ringOpen_{false};
    std::atomic<bool> gestureRequested_{false};
    std::atomic<bool> reinstall_{false};

    // Ring buffer to main (SPSC: this thread writes, main reads).
    static constexpr std::size_t kRing = 1024;
    std::array<InputEvent, kRing> ring_{};
    std::atomic<std::size_t> head_{0}, tail_{0};
    std::atomic<bool> posted_{false};

    // Thread-only state
    RawInput raw_;
    ChordDecoder chord_;
    Kinetic kinetic_;
    WheelEmitter wheelOut_;
    GestureAccumulator gestures_;
    bool gestureActive_ = false;
    POINT gestureAnchor_{};
    std::uint8_t surfaceButtons_ = 0;
    Seconds lastSurfaceWheel_ = -1e9, lastOtherWheel_ = -1e9;
    Seconds swallowLeftUpUntil_ = 0, swallowRightUpUntil_ = 0;
    POINT dragStart_{};
    double dragDistance_ = 0;
    bool atEdge_ = false;
    bool holdArmed_ = false, holdFired_ = false;
    POINT holdOrigin_{};
    Seconds holdDeadline_ = 0;
    std::array<RECT, 16> monitors_{};
    int monitorCount_ = 0;
    Seconds nextMonitorRefresh_ = 0;
    // Attribution FIFO of recent raw entries (content-matched by the hooks)
    struct RawEntry {
        RawKind kind;
        bool surface;
        std::int16_t wheel;
        std::uint16_t scan;
        std::uint16_t buttonFlags;
        Seconds at;
    };
    std::array<RawEntry, 32> fifo_{};
    std::size_t fifoHead_ = 0, fifoCount_ = 0;
    bool matchRaw(RawKind kind, std::int16_t wheel, std::uint16_t scan, std::uint16_t buttonFlags, Seconds now, bool& surface) noexcept;

    std::atomic<std::uint32_t> heartbeat_{0}, rawCount_{0};
    std::atomic<bool> hooksOk_{false};
    std::atomic<bool> leftDown_{false};
    std::atomic<std::int64_t> lastMoveQpc_{0};
};

}  // namespace ixpat::win
