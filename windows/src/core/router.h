#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/actions.h"
#include "core/chord.h"
#include "core/clock.h"
#include "core/events.h"
#include "core/haptic_table.h"
#include "core/settings.h"
#include "core/wheel_feel.h"

namespace ixpat {

constexpr int kTimerLeftHold = 1, kTimerRightHold = 2, kTimerFreeSpinIdle = 3;

/// What the router asks the platform to do. Every callback runs on the router's (main) thread.
struct RouterSinks {
    std::function<void(HapticEvent e, std::optional<int> ordinal, int boost)> play;  // -> engine.play(e, rule(e), ...)
    std::function<void(const ButtonAction&, bool hold)> runAction;                    // -> ActionRunner
    std::function<void(int id, int delayMs)> armTimer;                                // one-shot; fires Router::timer(id)
    std::function<void(int id)> cancelTimer;
    std::function<void()> gesturesBegin;                                              // freeze the pointer, start accumulating
    std::function<void()> gesturesEnd;
    std::function<void(bool holdMode)> ringOpen;                                      // open the Action Ring
    std::function<void()> ringToggle;                                                 // toggle mode (press)
    std::function<bool()> ringIsOpen;
    std::function<void()> ringHoldEnded;                                              // hold mode: commit on release
    std::function<std::optional<float>()> systemVolume;                               // 0..1, for the gesture limit bump
};

/// The Mac AppDelegate's event logic without the platform: device events in, rule plays, actions and
/// timer requests out. Owns the wheel feel state machine. Single-threaded (main).
class Router {
public:
    Router(Settings& settings, RouterSinks sinks);

    void setTable(std::span<const Waveform> table);

    // Device side (Raw Input events attributed to the Surface Mouse)
    void buttons(std::uint8_t oldMask, std::uint8_t newMask, Seconds now);  // bit0 L, bit1 R, bit2 wheel
    void wheel(int notches, Seconds now);
    void chord(ChordOutput out, Seconds now);
    /// A hold-and-move gesture produced an action (from GestureAccumulator::move).
    void gestureFired(const ButtonAction& action, Seconds now);

    // System side
    void systemEvent(HapticEvent e, Seconds now);
    void foregroundChanged(std::string imageName, Seconds now);  // lowercased into settings.frontBundleID
    void deviceConnected(bool on, Seconds now);
    void timer(int id, Seconds now);

    /// Run an action the way the action button would (ring and gesture kinds need router state).
    void run(const ButtonAction& a, bool hold);

    bool freeSpinActive() const { return wheel_.freeSpinActive(); }
    bool gestureModeActive() const { return gestureActive_; }
    std::uint8_t buttonsMask() const { return buttons_; }
    WheelFeelParams wheelParams() const;

private:
    void onGesture(ButtonGesture g, Seconds now);
    void holdEnded(Seconds now);
    void armHold(int id, HapticEvent holdEvent);
    int ordinalOr(std::uint16_t usage, int fallback) const;

    Settings& s_;
    RouterSinks k_;
    WheelFeelEngine wheel_;
    std::vector<Waveform> table_{measuredWaveforms().begin(), measuredWaveforms().end()};
    std::uint8_t buttons_ = 0;
    std::optional<ButtonAction> pendingHold_;
    bool gestureActive_ = false;
    int gestureFired_ = 0;
    bool chordHeld_ = false;
};

}  // namespace ixpat
