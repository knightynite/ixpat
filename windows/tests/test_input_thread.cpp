#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "win/input_thread.h"

// Requires an interactive Windows desktop with iXpat closed and no physical input.
// Run explicitly with --no-skip=true --test-case="desktop input:*".
// Only tagged releases of an unassigned key are sent; no typed keys are recorded.
TEST_CASE("desktop input: hook replacement and idle refresh deliver each event once" * doctest::skip()) {
    using namespace ixpat::win;
    using namespace std::chrono_literals;
    InputThread input;
    InputConfig cfg;
    cfg.enabled = cfg.swallowChords = false;
    input.setConfig(cfg);
    REQUIRE(input.start(nullptr, WM_APP + 1));
    REQUIRE(input.status().keyboardHook);
    REQUIRE(input.status().mouseHook);

    const auto before = input.status().keyboardHookCalls;
    constexpr int count = 2000;
    std::atomic<int> sent{0};
    std::thread sender([&] {
        INPUT event{};
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = 0xE8;
        event.ki.dwFlags = KEYEVENTF_KEYUP;
        event.ki.dwExtraInfo = kOwnTag;
        for (int i = 0; i < count; ++i) {
            sent += static_cast<int>(SendInput(1, &event, sizeof event));
            std::this_thread::sleep_for(5ms);
        }
    });
    for (int i = 0; i < 200; ++i) {
        input.reinstallHooks();
        std::this_thread::sleep_for(50ms);
    }
    sender.join();
    CHECK(sent == count);
    CHECK(input.status().keyboardHookCalls - before == count);
    CHECK(input.status().hookReinstalls >= 10);
    CHECK(input.status().hookInstallError == 0);

    // Let passive refresh run without resetting Windows' idle timer with probes.
    std::this_thread::sleep_for(3500ms);
    const auto afterIdle = input.status().keyboardHookCalls;
    queueHookProbe();
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (input.status().keyboardHookCalls == afterIdle && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);
    CHECK(input.status().keyboardHookCalls == afterIdle + 1);
    CHECK(input.status().hookInstallError == 0);
    input.stop();
}
