#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ixpat {

/// One entry of the device's HID Simple Haptic Controller waveform table.
struct Waveform {
    int ordinal;          // what goes in the Manual Trigger field of output report 0x43
    std::uint16_t usage;  // HID haptics usage (0x1003 = Click ...)
    int durationMs;
    std::string_view name() const;
};

/// Table read from a Surface Mouse 2nd Edition (fw 5.29) on 2026-10-16; used until the device answers.
std::span<const Waveform> measuredWaveforms();

/// Feature report 0x42: [id][50 x u16 durations (ms)][50 x u16 usages]; entries with a usage in
/// 0x1003...0x2FFF become waveforms with ordinal = index + 3 (MouseDevice.readHapticTable).
std::vector<Waveform> parseHapticTable(std::span<const std::uint8_t> feature42);

std::optional<int> ordinalFor(std::uint16_t usage, std::span<const Waveform> table);
std::string_view waveformName(std::uint16_t usage);  // "Click", or "Waveform 0x1234"

/// Bridge names: a lowercase waveform name ("align") or an alias from plugins/README.md
/// (snap, guide, detent, tick, limit, done, fail, notify, select, press).
std::optional<std::uint16_t> usageForLowercaseName(std::string_view name);

}  // namespace ixpat
