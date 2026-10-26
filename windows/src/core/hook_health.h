#pragma once
#include <algorithm>

#include "core/clock.h"

namespace ixpat {

// A successful probe send without a callback is evidence even before the first reply.
// Repeated failures back off, but recovery is never disabled for the rest of a session.
class HookHealth {
public:
    bool observe(bool answered, Seconds now) {
        if (answered) {
            misses_ = 0;
            delay_ = 2.0;
            return false;
        }
        misses_ = std::min(misses_ + 1, 2);
        if (misses_ < 2 || now < retryAt_) return false;
        misses_ = 0;
        retryAt_ = now + delay_;
        delay_ = std::min(delay_ * 2, 30.0);
        return true;
    }

private:
    int misses_ = 0;
    Seconds retryAt_ = 0, delay_ = 2.0;
};

}  // namespace ixpat
