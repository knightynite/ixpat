#pragma once
#include <array>
#include <cstdint>

#include "core/actions.h"

namespace ixpat {

/// Which action each direction runs. Only the kind is needed here (stepped or not); the full
/// ButtonAction lives in Settings and is looked up by direction when an action fires. Plain data, so
/// the input thread's snapshot copy never allocates.
struct GestureConfig {
    ActionKind up = ActionKind::volumeUp;
    ActionKind down = ActionKind::volumeDown;
    ActionKind left = ActionKind::previousTrack;
    ActionKind right = ActionKind::nextTrack;
    double stepPx = 24;   // pointer travel per volume / brightness step
    double swipePx = 90;  // pointer travel per swipe action
    bool freezePointer = true;
};

/// Direction indices used by MoveResult and the app wiring.
enum GestureDir : std::uint8_t { kGestureUp = 0, kGestureDown = 1, kGestureLeft = 2, kGestureRight = 3 };

/// Hold-and-move gestures, ported from GestureController: while the action button is held the
/// pointer is frozen and its motion drives actions instead. Vertical motion steps (volume,
/// brightness) every `stepPx`; horizontal motion swipes (track change, desktops) every `swipePx`.
/// The first 10 px decide the axis, which then stays locked for the hold.
///
/// Allocation-free: safe to call from the input thread's hook callback.
class GestureAccumulator {
public:
    struct MoveResult {
        bool consumed = false;             // true while a hold is active: the pointer stays put
        int count = 0;                     // 0...6 directions fired, in order
        std::array<std::uint8_t, 6> dirs{};
    };

    void begin();
    void end();
    bool active() const { return active_; }
    /// Actions fired during the current / last hold (a hold with none is a plain hold).
    int firedCount() const { return fired_; }
    MoveResult move(double dx, double dy, const GestureConfig& c);

private:
    static constexpr double kAxisLockPx = 10;
    static constexpr int kMaxPerMove = 6;

    bool active_ = false;
    double accX_ = 0, accY_ = 0;
    int axis_ = 0;  // 0 undecided, 1 horizontal, 2 vertical
    int fired_ = 0;
};

}  // namespace ixpat
