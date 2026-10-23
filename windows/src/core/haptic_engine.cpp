#include "core/haptic_engine.h"

#include <algorithm>
#include <cmath>

namespace ixpat {

void HapticEngine::setTable(std::span<const Waveform> table) { table_.assign(table.begin(), table.end()); }

int HapticEngine::durationMs(int ordinal) const {
    for (const auto& w : table_) {
        if (w.ordinal == ordinal) return w.durationMs;
    }
    return kUnknownDurationMs;
}

std::string HapticEngine::nameOf(int ordinal) const {
    for (const auto& w : table_) {
        if (w.ordinal == ordinal) return std::string(w.name());
    }
    return "#" + std::to_string(ordinal);
}

bool HapticEngine::play(int ordinal, int intensity, std::string_view label, Seconds now) {
    if (!connected_) return false;
    // The device plays one waveform at a time; don't pile triggers up.
    const double window = std::max(durationMs(ordinal) / 1000.0, kMinSpacing);
    if (now - lastAny_ < window) return false;
    lastAny_ = now;
    if (trigger) trigger(Play{ordinal, intensity}, label);
    ++pulseCount_;
    lastPlayed_ = std::string(label) + " \xC2\xB7 " + nameOf(ordinal) + " \xC2\xB7 " + std::to_string(intensity);
    return true;
}

void HapticEngine::play(HapticEvent e, const HapticRule& rule, const EngineParams& p, Seconds now,
                        std::optional<int> ordinal, int boost) {
    if (!p.enabled || !p.effHapticsEnabled) return;
    if (!rule.enabled) return;
    lastEvent_ = std::string(title(e));
    // Master strength scales the rule's own (both 0...4). A low master setting must soften light rules,
    // not silence them: anything enabled plays at least level 1.
    if (p.masterIntensity <= 0 || rule.intensity <= 0) return;
    auto scaled = [&](int ruleIntensity) {
        const int base = std::clamp(ruleIntensity + boost, 1, 4);
        return std::max(1, static_cast<int>(std::round(base * p.masterIntensity / 4.0)));
    };
    play(ordinal.value_or(rule.ordinal), scaled(rule.intensity), title(e), now);
    if (rule.echo && rule.echo->intensity > 0 && defer) {
        defer(Deferred{std::max(20, rule.echo->delayMs), Play{rule.echo->ordinal, scaled(rule.echo->intensity)},
                       std::string(title(e)) + " echo"});
    }
}

void HapticEngine::preview(const HapticRule& r, std::string_view label, Seconds now) {
    play(r.ordinal, r.intensity, label, now);
    if (r.echo && defer) {
        defer(Deferred{std::max(20, r.echo->delayMs), Play{r.echo->ordinal, r.echo->intensity},
                       std::string(label) + " echo"});
    }
}

}  // namespace ixpat
