#pragma once
#include <vector>

#include "core/clock.h"

namespace ixpat {

/// Kinetic scrolling parameters (the Mac's `SmoothConfig`).
struct KineticConfig {
    bool enabled = false;
    double pxPerNotch = 60;  // distance one slow notch travels
    double glide = 5;        // 1...10: how long a single slow notch takes to settle
    double momentum = 4;     // 0...10: how far a fast spin coasts after the wheel stops (0 = none)
    double accel = 1;        // 0...3: extra distance per notch when spinning fast
    double multiplier = 1;   // free-spin boost
    bool reverse = false;
};

/// Momentum for a notched wheel, ported from EventMonitor.kineticNotch / scrollTick.
///
/// Every notch is a velocity impulse; the velocity decays with a friction time constant `tau` that
/// follows how fast the wheel is spinning. A lone slow notch uses a short tau and travels exactly
/// pxPerNotch, so it settles crisply; a fast spin stretches tau so the content keeps coasting after
/// the wheel stops, and an acceleration gain makes each of those notches worth more pixels.
///
/// Single-threaded: the owner calls `notch` and `tick` from one thread (the input thread).
class Kinetic {
public:
    /// One wheel event, in notches per axis (a 120-unit Windows delta is one notch). Caller decides
    /// whether the event belongs to this mouse and whether smoothing is on for it.
    void notch(double notchesY, double notchesX, Seconds now, const KineticConfig& cfg);

    struct Step {
        int dy = 0;
        int dx = 0;
        bool active = false;  // false once the coast has come to rest
    };
    /// Integrate the coast since the previous tick; call at ~120 Hz while `active()`.
    Step tick(Seconds now);

    bool active() const { return running_; }
    /// A click or key press kills the coast.
    void stop();
    /// stop() and forget the notch history.
    void reset();

private:
    void kick(double& vel, double n, double sign, double wheelRate, double tauSlow, double tauFast,
              const KineticConfig& cfg, Seconds now);

    double velX_ = 0, velY_ = 0;      // px/s
    double carryX_ = 0, carryY_ = 0;  // sub-pixel remainder
    double tau_ = 0.05, tauTarget_ = 0.05;
    Seconds lastTick_ = 0;
    bool running_ = false;
    std::vector<Seconds> notchTimes_;
};

}  // namespace ixpat
