#pragma once
#include <cmath>
#include <optional>

namespace ixpat {

/// Action Ring geometry, from RadialMenuController. Chamber 0 is centred at 12 o'clock; chambers go
/// clockwise. Angles are atan2(dy, dx) in screen coordinates (y down).
constexpr double kRingDeadZone = 26.0;  // px around the origin where nothing is selected
constexpr int kRingSize = 360;          // px, the ring window's side

inline int ringSlotIndex(double angleRadians, int count) {
    if (count <= 0) return 0;
    const double pi = 3.14159265358979323846;
    double a = angleRadians + pi / 2;  // 0 at top
    if (a < 0) a += 2 * pi;
    const double slice = 2 * pi / count;
    return static_cast<int>(std::floor((a + slice / 2) / slice)) % count;
}

/// The chamber under a pointer offset from the ring's origin, or none inside the dead zone.
inline std::optional<int> ringSelect(double dx, double dy, int count) {
    if (count <= 0) return std::nullopt;
    if (std::hypot(dx, dy) <= kRingDeadZone) return std::nullopt;
    return ringSlotIndex(std::atan2(dy, dx), count);
}

}  // namespace ixpat
