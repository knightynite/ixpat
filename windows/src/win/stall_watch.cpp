#include "win/stall_watch.h"

namespace ixpat::win {
namespace {
constexpr DWORD kStepMs = 50;       // how often the watch wakes
constexpr double kStallMs = 100;    // a wake this much later than asked is a stall worth reporting
}  // namespace

StallWatch::~StallWatch() { stop(); }

void StallWatch::start(HWND owner, UINT message) {
    if (thread_.joinable()) return;
    owner_ = owner;
    message_ = message;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { loop(); });
}

void StallWatch::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void StallWatch::loop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    LARGE_INTEGER f, last;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&last);
    for (;;) {
        if (WaitForSingleObject(stopEvent_, kStepMs) == WAIT_OBJECT_0) return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double lateMs = static_cast<double>(now.QuadPart - last.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart) - kStepMs;
        last = now;
        if (lateMs < kStallMs) continue;
        const auto ms = static_cast<std::uint32_t>(lateMs);
        std::uint32_t cur = maxMs_.load(std::memory_order_relaxed);
        while (ms > cur && !maxMs_.compare_exchange_weak(cur, ms, std::memory_order_relaxed)) {
        }
        count_.fetch_add(1, std::memory_order_relaxed);
        if (owner_) PostMessageW(owner_, message_, ms, 0);
    }
}

}  // namespace ixpat::win
