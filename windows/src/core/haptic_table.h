#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ixpat {

/// One entry of the device's HID Simple Haptic Controller waveform table.
struct Waveform {
    int ordinal;          // what goes in the Manual Trigger field of output report 0x43
    std::uint16_t usage;  // HID haptics usage (0x1003 = Click ...)
    int durationMs;
    std::string name() const;
};

/// Table read from a Surface Mouse 2nd Edition (fw 5.29) on 2026-10-16; used until the device answers.
std::span<const Waveform> measuredWaveforms();

/// Feature report 0x42: [id][50 x u16 durations (ms)][50 x u16 usages]; entries with a usage in
/// 0x1003...0x2FFF become waveforms with ordinal = index + 3 (MouseDevice.readHapticTable).
std::vector<Waveform> parseHapticTable(std::span<const std::uint8_t> feature42);

std::optional<int> ordinalFor(std::uint16_t usage, std::span<const Waveform> table);
std::string waveformName(std::uint16_t usage);  // "Click", or "Waveform 0x1234"

/// Bridge names: a lowercase waveform name ("align") or a semantic alias from Bridge.swift
/// (snap, guide, detent, tick, boundary, limit, edge, done, complete, fail, notify, progress, select, focus, press).
std::optional<std::uint16_t> usageForLowercaseName(std::string_view name);
/// The alias target's lowercase name ("snap" -> "align"), or the input unchanged when it is not an alias.
std::string canonicalBridgeName(std::string_view lowercaseName);

}  // namespace ixpat
