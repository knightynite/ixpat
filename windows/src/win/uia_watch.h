#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "core/clock.h"
#include "core/events.h"

namespace ixpat::win {

/// Which accessibility-driven ticks are on (MenuWatcher.Config + AXWatcher.Config on the Mac).
struct UiaConfig {
    bool menuTicks = false, textTicks = false, controlTicks = false, dropTicks = false;
    bool sliderTicks = false, selectionTicks = false, scrollEndTicks = false;
    bool any() const { return menuTicks || textTicks || controlTicks || dropTicks || sliderTicks || selectionTicks || scrollEndTicks; }
};

/// Counters for Diagnostics.
struct UiaStats {
    std::uint32_t polls = 0, menu = 0, control = 0, drop = 0, word = 0, line = 0, slider = 0, selection = 0, scrollEnd = 0;
};

/// UI Automation hit-testing on a worker thread (MTA): the highlighted menu item, controls under the
/// pointer, drop targets under a drag, the growing text selection, slider detents, selection changes
/// after a click and the scroll bar reaching an end after a wheel notch. One cross-process call per
/// poll (a cache request), 50 ms while the pointer is active, nothing while it is idle.
class UiaWatch {
public:
    UiaWatch();
    ~UiaWatch();
    UiaWatch(const UiaWatch&) = delete;
    UiaWatch& operator=(const UiaWatch&) = delete;

    /// Ticks arrive as PostMessage(hwnd, message, (WPARAM)HapticEvent, 0).
    void start(HWND hwnd, UINT message);
    void stop();
    void setConfig(const UiaConfig& c);
    /// Thread-safe inputs from the input thread / main.
    std::function<Seconds()> lastPointerMove;  // monotonic seconds
    std::function<bool()> leftButtonDown;
    std::function<Seconds()> now;
    /// A left click happened (selection check) / a wheel notch happened (scroll-end check).
    void noteClick();
    void noteWheel();
    UiaStats stats() const;

private:
    void loop();
    void tick();

    HWND hwnd_ = nullptr;
    UINT message_ = 0;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    UiaConfig cfg_;
    UiaStats stats_;
    std::atomic<bool> clickPending_{false}, wheelPending_{false};
    std::atomic<std::int64_t> clickAtMs_{0}, wheelAtMs_{0};
    std::atomic<int> wheelStage_{0};
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace ixpat::win
