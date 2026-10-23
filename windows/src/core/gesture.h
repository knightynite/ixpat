#pragma once
#include <vector>

#include "core/actions.h"

namespace ixpat {

struct GestureConfig {
    ButtonAction up = ButtonAction::blank(ActionKind::volumeUp);
    ButtonAction down = ButtonAction::blank(ActionKind::volumeDown);
    ButtonAction left = ButtonAction::blank(ActionKind::previousTrack);
    ButtonAction right = ButtonAction::blank(ActionKind::nextTrack);
    double stepPx = 24;   // pointer travel per volume / brightness step
    double swipePx = 90;  // pointer travel per swipe action
    bool freezePointer = true;
};

/// Hold-and-move gestures, ported from GestureController: while the action button is held the
/// pointer is frozen and its motion drives actions instead. Vertical motion steps (volume,
/// brightness) every `stepPx`; horizontal motion swipes (track change, desktops) every `swipePx`.
/// The first 10 px decide the axis, which then stays locked for the hold.
///
/// Single-threaded from the caller's point of view: begin()/end() and move() are serialised by the
/// owner (the input thread in the app).
class GestureAccumulator {
public:
    struct MoveResult {
        bool consumed = false;              // true while a hold is active: the pointer stays put
        std::vector<ButtonAction> actions;  // 0...6 actions to run, in order
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
