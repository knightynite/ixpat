#include "core/wheel_feel.h"

#include <algorithm>
#include <cstdlib>

namespace ixpat {

std::vector<WheelPulse> WheelFeelEngine::notch(int delta, Seconds now, const WheelFeelParams& p) {
    std::vector<WheelPulse> out;
    if (!p.enabled) {
        freeSpin_ = false;  // nothing plays while disabled; drop out of free-spin silently
        return out;
    }
    const Seconds gap = now - lastNotchTime_;
    lastNotchTime_ = now;
    notchTimes_.push_back(now);
    std::erase_if(notchTimes_, [&](Seconds t) { return now - t > kRateWindow; });
    const double rate = static_cast<double>(notchTimes_.size()) / kRateWindow;  // notches per second

    // Free-spin: a flick past the threshold silences the ratchet and boosts distance until the wheel
    // rests; the ratchet re-engages with a soft Align (see idle()).
    if (p.freeSpin) {
        if (!freeSpin_ && rate >= p.freeSpinThreshold) {
            freeSpin_ = true;
            out.push_back({HapticEvent::freeSpinOn, std::nullopt, 0});
        }
    } else if (freeSpin_) {
        freeSpin_ = false;
    }
    if (freeSpin_) {  // free-spin: no ratchet, optional grain
        textureAccum_ += std::abs(delta);
        if (textureAccum_ >= std::max(1, p.freeSpinTextureEvery) && now - lastRatchetTick_ >= kLightSpacing) {
            textureAccum_ = 0;
            lastRatchetTick_ = now;
            out.push_back({HapticEvent::freeSpinTexture, std::nullopt, 0});
        }
        return out;
    }
    textureAccum_ = 0;

    // Direction reversal while scrolling: one firmer Collide bump, like a detent catching.
    const int dir = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
    if (p.ratchetDynamic && dir != 0 && lastNotchDir_ != 0 && dir != lastNotchDir_ && gap < 1.0) {
        lastNotchDir_ = dir;
        wheelAccum_ = 0;
        out.push_back({HapticEvent::scrollStep, p.collideOrdinal, 1});
        return out;
    }
    lastNotchDir_ = dir;
    wheelAccum_ += std::abs(delta);
    const int every = std::max(1, p.scrollStepEvery);
    if (wheelAccum_ < every) return out;
    wheelAccum_ = 0;
    if (!p.ratchetDynamic) {
        out.push_back({HapticEvent::scrollStep, std::nullopt, 0});
        return out;
    }
    if (rate >= p.ratchetLightRate) {
        // Fast: short light ticks, capped at 25/s so they stay distinct instead of smearing.
        if (now - lastRatchetTick_ < kLightSpacing) return out;
        lastRatchetTick_ = now;
        // Step, or Hover if the rule itself is Step.
        const int light = p.scrollStepOrdinal == p.stepOrdinal ? p.hoverOrdinal : p.stepOrdinal;
        out.push_back({HapticEvent::scrollStep, light, -1});
    } else {
        // Slow: a firm detent per notch; the first notch after a pause lands a touch harder.
        lastRatchetTick_ = now;
        out.push_back({HapticEvent::scrollStep, std::nullopt, gap > 0.6 ? 1 : 0});
    }
    return out;
}

std::optional<WheelPulse> WheelFeelEngine::idle(Seconds now, const WheelFeelParams& p) {
    if (!freeSpin_ || now - lastNotchTime_ < kRestTime) return std::nullopt;
    freeSpin_ = false;
    if (!p.enabled) return std::nullopt;
    return WheelPulse{HapticEvent::freeSpinOff, std::nullopt, 0};
}

void WheelFeelEngine::reset() {
    notchTimes_.clear();
    lastNotchTime_ = 0;
    lastRatchetTick_ = 0;
    lastNotchDir_ = 0;
    wheelAccum_ = 0;
    textureAccum_ = 0;
    freeSpin_ = false;
}

}  // namespace ixpat
