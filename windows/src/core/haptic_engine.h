#pragma once
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/clock.h"
#include "core/events.h"
#include "core/haptic_table.h"

namespace ixpat {

/// The settings the engine consults (Settings.enabled, effHapticsEnabled, masterIntensity).
struct EngineParams {
    bool enabled = true;
    bool effHapticsEnabled = true;
    int masterIntensity = 4;
};

/// Plays device waveforms with rate limiting, master scaling and echoes (HapticEngine.swift).
/// Pure: `trigger` sends the report, `defer` schedules the echo; the owner calls back into
/// `play(ordinal, intensity, label, now)` when the deferred time comes.
class HapticEngine {
public:
    struct Play {
        int ordinal;
        int intensity;
    };
    struct Deferred {
        int delayMs;
        Play play;
        std::string label;
    };
    std::function<void(Play, std::string_view label)> trigger;
    std::function<void(Deferred)> defer;

    void setTable(std::span<const Waveform> table);
    void setConnected(bool on) { connected_ = on; }
    bool connected() const { return connected_; }

    /// Fire one waveform now (test buttons, echoes, the bridge). False when dropped.
    bool play(int ordinal, int intensity, std::string_view label, Seconds now);
    /// Fire the waveform configured for an event, honouring the master switches. `ordinal` swaps the
    /// rule's waveform for this play; `boost` shifts the rule's intensity before master scaling.
    void play(HapticEvent e, const HapticRule& rule, const EngineParams& p, Seconds now,
              std::optional<int> ordinal = std::nullopt, int boost = 0);
    /// Preview a rule as the user would feel it (pulse + echo), unscaled.
    void preview(const HapticRule& r, std::string_view label, Seconds now);

    int pulseCount() const { return pulseCount_; }
    const std::string& lastPlayed() const { return lastPlayed_; }
    const std::string& lastEvent() const { return lastEvent_; }

private:
    static constexpr Seconds kMinSpacing = 0.035;
    static constexpr int kUnknownDurationMs = 50;
    int durationMs(int ordinal) const;
    std::string nameOf(int ordinal) const;

    std::vector<Waveform> table_{measuredWaveforms().begin(), measuredWaveforms().end()};
    bool connected_ = false;
    Seconds lastAny_ = -1e9;
    int pulseCount_ = 0;
    std::string lastPlayed_ = "-";
    std::string lastEvent_ = "-";
};

}  // namespace ixpat
