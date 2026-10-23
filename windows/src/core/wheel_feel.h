#pragma once
#include <optional>
#include <vector>

#include "core/clock.h"
#include "core/events.h"

namespace ixpat {

/// What the wheel-feel state machine needs from the settings and the device table.
struct WheelFeelParams {
    bool enabled = true;          // settings.enabled
    bool freeSpin = true;         // effFreeSpin
    bool ratchetDynamic = true;   // effRatchetDynamic
    double freeSpinThreshold = 18;  // notches per second
    double ratchetLightRate = 10;   // notches per second where ticks go light
    int scrollStepEvery = 1;        // effScrollStepEvery
    int freeSpinTextureEvery = 2;   // effFreeSpinTextureEvery
    int scrollStepOrdinal = 6;      // rule(.scrollStep).ordinal: decides Step vs Hover for light ticks
    int collideOrdinal = 4;         // from the device table (fallbacks are the measured ordinals)
    int stepOrdinal = 6;
    int hoverOrdinal = 3;
};

/// A pulse the engine should play: the event, an optional waveform override, an intensity shift.
struct WheelPulse {
    HapticEvent event;
    std::optional<int> ordinal;
    int boost = 0;
};

/// The wheel's feel, ported from the Mac AppDelegate's `device.onWheel` handler: dynamic ratchet
/// (firm when slow, light when fast, a bump on reversal), free-spin after a flick with an optional
/// texture, and the ratchet re-engaging after 300 ms of rest.
class WheelFeelEngine {
public:
    /// One wheel report (`delta` in notches, sign = direction). Returns the pulses to play, in order.
    std::vector<WheelPulse> notch(int delta, Seconds now, const WheelFeelParams& p);
    /// Call as time passes (a 300 ms timer after each notch): the free-spin ends when the wheel rests.
    std::optional<WheelPulse> idle(Seconds now, const WheelFeelParams& p);
    bool freeSpinActive() const { return freeSpin_; }
    void reset();

private:
    static constexpr Seconds kRateWindow = 0.25;
    static constexpr Seconds kRestTime = 0.30;
    static constexpr Seconds kLightSpacing = 0.04;  // light ticks and texture capped at 25/s

    std::vector<Seconds> notchTimes_;
    Seconds lastNotchTime_ = 0;
    Seconds lastRatchetTick_ = 0;
    int lastNotchDir_ = 0;
    int wheelAccum_ = 0;
    int textureAccum_ = 0;
    bool freeSpin_ = false;
};

}  // namespace ixpat
