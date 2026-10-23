#include "core/kinetic.h"

#include <algorithm>
#include <cmath>

namespace ixpat {
namespace {

constexpr double kRateWindow = 0.25;  // seconds of notch history that define the wheel rate
constexpr double kStopSpeed = 40;     // px/s: below ~1/3 px per frame the coast is done
constexpr double kBrakeEase = 0.08;   // s: extra friction eases in over this, never in one frame

double capNotches(double n) { return n == 0 ? 0 : std::clamp(n, -2.0, 2.0); }  // cap the OS line ramp

}  // namespace

void Kinetic::notch(double notchesY, double notchesX, Seconds now, const KineticConfig& cfg) {
    if (!cfg.enabled) return;
    notchTimes_.push_back(now);
    std::erase_if(notchTimes_, [&](Seconds t) { return now - t > kRateWindow; });
    const double wheelRate = static_cast<double>(notchTimes_.size()) / kRateWindow;  // notches per second
    const double tauSlow = 0.015 + cfg.glide * 0.008;                                // 0.023 ... 0.095 s
    const double tauFast = std::max(tauSlow, cfg.momentum * 0.12);                   // up to 1.2 s of coast
    const double sign = cfg.reverse ? -1.0 : 1.0;
    if (!running_) {
        running_ = true;
        lastTick_ = 0;  // the first tick integrates one nominal frame
    }
    kick(velY_, capNotches(notchesY), sign, wheelRate, tauSlow, tauFast, cfg, now);
    kick(velX_, capNotches(notchesX), sign, wheelRate, tauSlow, tauFast, cfg, now);
}

// Distance of an impulse dv under exponential decay is dv*tau, so dv = px/tau.
void Kinetic::kick(double& vel, double n, double sign, double wheelRate, double tauSlow, double tauFast,
                   const KineticConfig& cfg, Seconds now) {
    if (n == 0) return;
    const double dir = n * sign;
    if (vel != 0 && (dir > 0) != (vel > 0)) {
        // Reversing kills the coast and starts a fresh, slow gesture: one plain notch.
        vel = 0;
        notchTimes_.assign(1, now);
        tau_ = tauSlow;
        tauTarget_ = tauSlow;
        vel += dir * cfg.pxPerNotch * cfg.multiplier / tau_;
        return;
    }
    // How fast the wheel "is" spinning: the notch rate of the last quarter second, or, when the
    // content is still coasting from an earlier flick, the wheel rate that coast is equivalent to.
    // Without the second term a notch during a coast reads as a lone slow notch, tau collapses, and
    // the content slams to a stop before jerking on.
    const double coastRate = std::abs(vel) / (cfg.pxPerNotch * cfg.multiplier);
    const double rate = std::max(wheelRate, coastRate);
    const double fast = std::clamp((rate - 5) / 20, 0.0, 1.0);  // 0 at <= 5/s ... 1 at >= 25/s
    tauTarget_ = tauSlow + (tauFast - tauSlow) * fast;
    if (tauTarget_ > tau_) tau_ = tauTarget_;  // a flick takes hold at once...
    const double gain = (1 + cfg.accel * fast) * cfg.multiplier;
    vel += dir * cfg.pxPerNotch * gain / tau_;
}

Kinetic::Step Kinetic::tick(Seconds now) {
    Step out;
    if (!running_) return out;
    // Integrate over the real elapsed time, so a late timer does not slow the coast.
    const double dt = lastTick_ > 0 ? std::clamp(now - lastTick_, 1.0 / 240, 0.05) : 1.0 / 120;
    lastTick_ = now;
    if (std::abs(velX_) < kStopSpeed && std::abs(velY_) < kStopSpeed) {
        velX_ = velY_ = carryX_ = carryY_ = 0;
        running_ = false;
        return out;
    }
    // ...while extra friction eases in over ~80 ms, so the coast never brakes in one frame.
    if (tauTarget_ < tau_) tau_ += (tauTarget_ - tau_) * (1 - std::exp(-dt / kBrakeEase));
    const double decay = std::exp(-dt / tau_);
    auto step = [&](double& vel, double& carry) -> int {
        if (vel == 0) return 0;
        const double s = vel * tau_ * (1 - decay);  // exact integral over the frame
        vel *= decay;
        const double total = s + carry;
        const double whole = std::trunc(total);
        carry = total - whole;
        return static_cast<int>(std::clamp(whole, -1e6, 1e6));
    };
    out.dy = step(velY_, carryY_);
    out.dx = step(velX_, carryX_);
    out.active = true;
    return out;
}

void Kinetic::stop() {
    velX_ = velY_ = carryX_ = carryY_ = 0;
    running_ = false;
}

void Kinetic::reset() {
    stop();
    notchTimes_.clear();
    tau_ = tauTarget_ = 0.05;
    lastTick_ = 0;
}

}  // namespace ixpat
