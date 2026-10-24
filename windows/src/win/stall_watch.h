#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace ixpat::win {

/// Watches for the whole machine standing still. A thread sleeps in short steps and measures how late each
/// step wakes; a step that wakes a long time late means every thread stood still, which is also how Windows
/// comes to drop low-level hooks (a callback it could not deliver in time). Each stall is reported to the
/// owner's window as `message` with the length in milliseconds in wParam.
class StallWatch {
public:
    StallWatch() = default;
    ~StallWatch();
    StallWatch(const StallWatch&) = delete;
    StallWatch& operator=(const StallWatch&) = delete;

    void start(HWND owner, UINT message);
    void stop();
    std::uint32_t maxStallMs() const { return maxMs_.load(std::memory_order_relaxed); }
    std::uint32_t count() const { return count_.load(std::memory_order_relaxed); }

private:
    void loop();

    HWND owner_ = nullptr;
    UINT message_ = 0;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::atomic<std::uint32_t> maxMs_{0}, count_{0};
};

}  // namespace ixpat::win
