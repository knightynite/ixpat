#include "core/haptic_table.h"

#include <array>
#include <cstdio>

namespace ixpat {
namespace {

struct Named {
    std::uint16_t usage;
    std::string_view name;
};
constexpr std::array<Named, 15> kNames{{
    {0x1001, "None"}, {0x1002, "Stop"}, {0x1003, "Click"}, {0x1004, "Buzz"}, {0x1005, "Rumble"},
    {0x1006, "Press"}, {0x1007, "Release"}, {0x1008, "Hover"}, {0x1009, "Success"}, {0x100A, "Error"},
    {0x100B, "Ink"}, {0x1012, "Collide"}, {0x1013, "Align"}, {0x1014, "Step"}, {0x1015, "Grow"},
}};

// Semantic aliases plugins may use, exactly Bridge.swift's table. Checked before the names, so
// "press" is Click (what the Mac plays), not HID usage 0x1006.
struct Alias {
    std::string_view name;
    std::uint16_t usage;
};
constexpr std::array<Alias, 15> kAliases{{
    {"snap", 0x1013}, {"guide", 0x1013},                            // align
    {"detent", 0x1014}, {"tick", 0x1014},                           // step
    {"boundary", 0x1012}, {"limit", 0x1012}, {"edge", 0x1012},      // collide
    {"done", 0x1009}, {"complete", 0x1009},                         // success
    {"fail", 0x100A},                                               // error
    {"notify", 0x1015}, {"progress", 0x1015},                       // grow
    {"select", 0x1008}, {"focus", 0x1008},                          // hover
    {"press", 0x1003},                                              // click
}};

constexpr std::array<Waveform, 8> kMeasured{{
    {3, 0x1008, 9}, {4, 0x1012, 26}, {5, 0x1013, 9}, {6, 0x1014, 11},
    {7, 0x1015, 200}, {8, 0x1009, 70}, {9, 0x100A, 147}, {10, 0x1003, 11},
}};

std::string lowercase(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    return out;
}

}  // namespace

std::string Waveform::name() const { return waveformName(usage); }

std::span<const Waveform> measuredWaveforms() { return kMeasured; }

std::vector<Waveform> parseHapticTable(std::span<const std::uint8_t> raw) {
    std::vector<Waveform> list;
    if (raw.size() < 201) return list;
    for (int i = 0; i < 50; ++i) {
        const int dur = raw[1 + 2 * i] | (raw[2 + 2 * i] << 8);
        const std::uint16_t usage = static_cast<std::uint16_t>(raw[101 + 2 * i] | (raw[102 + 2 * i] << 8));
        if (usage >= 0x1003 && usage <= 0x2FFF) list.push_back({i + 3, usage, dur});
    }
    return list;
}

std::optional<int> ordinalFor(std::uint16_t usage, std::span<const Waveform> table) {
    for (const auto& w : table) {
        if (w.usage == usage) return w.ordinal;
    }
    return std::nullopt;
}

std::string waveformName(std::uint16_t usage) {
    for (const auto& n : kNames) {
        if (n.usage == usage) return std::string(n.name);
    }
    char buf[24];
    std::snprintf(buf, sizeof buf, "Waveform 0x%04X", usage);
    return buf;
}

std::optional<std::uint16_t> usageForLowercaseName(std::string_view name) {
    for (const auto& a : kAliases) {
        if (a.name == name) return a.usage;
    }
    for (const auto& n : kNames) {
        if (lowercase(n.name) == name) return n.usage;
    }
    return std::nullopt;
}

std::string canonicalBridgeName(std::string_view lowercaseName) {
    for (const auto& a : kAliases) {
        if (a.name == lowercaseName) return lowercase(waveformName(a.usage));
    }
    return std::string(lowercaseName);
}

}  // namespace ixpat
