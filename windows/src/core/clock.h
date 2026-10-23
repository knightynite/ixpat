#pragma once

namespace ixpat {

/// Time as the engine sees it: monotonic seconds. Every core unit takes `Seconds` arguments
/// instead of reading a clock, so tests drive it deterministically.
using Seconds = double;

struct MonotonicClock {
    virtual ~MonotonicClock() = default;
    virtual Seconds now() const = 0;
};

}  // namespace ixpat
