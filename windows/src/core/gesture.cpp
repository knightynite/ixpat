#include "core/gesture.h"

#include <cmath>

namespace ixpat {

void GestureAccumulator::begin() {
    if (active_) return;
    accX_ = accY_ = 0;
    axis_ = 0;
    fired_ = 0;
    active_ = true;
}

void GestureAccumulator::end() { active_ = false; }

GestureAccumulator::MoveResult GestureAccumulator::move(double dx, double dy, const GestureConfig& c) {
    MoveResult r;
    if (!active_) return r;
    r.consumed = true;
    accX_ += dx;
    accY_ += dy;
    if (axis_ == 0) {
        if (std::hypot(accX_, accY_) < kAxisLockPx) return r;
        axis_ = std::abs(accX_) >= std::abs(accY_) ? 1 : 2;
        accX_ = accY_ = 0;
        return r;
    }
    double& acc = axis_ == 1 ? accX_ : accY_;
    const std::uint8_t dir = axis_ == 1 ? (acc < 0 ? kGestureLeft : kGestureRight) : (acc < 0 ? kGestureUp : kGestureDown);
    const ActionKind kind = dir == kGestureUp ? c.up : dir == kGestureDown ? c.down : dir == kGestureLeft ? c.left : c.right;
    const double unit = isSteppedKind(kind) ? c.stepPx : c.swipePx;
    if (!(unit > 0)) return r;
    while (std::abs(acc) >= unit && r.count < kMaxPerMove) {
        acc -= acc < 0 ? -unit : unit;
        r.dirs[static_cast<std::size_t>(r.count++)] = dir;
        ++fired_;
    }
    return r;
}

}  // namespace ixpat
