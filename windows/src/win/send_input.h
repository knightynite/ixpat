#pragma once
#include <windows.h>

#include <cstdint>
#include <initializer_list>

namespace ixpat::win {

/// Marks everything we synthesise (dwExtraInfo), so the hooks let it through untouched.
constexpr ULONG_PTR kOwnTag = 0x49585054;    // "IXPT"
constexpr ULONG_PTR kProbeTag = 0x49585050;  // "IXPP": the hook probes; the hooks note them and eat them
inline bool isOurs(ULONG_PTR extraInfo) { return extraInfo == kOwnTag || extraInfo == kProbeTag; }

/// Pixels -> WHEEL_DELTA units with sub-unit carry, so apps that only scroll by lines still get the
/// right total and apps with smooth scrolling get the fine stream.
class WheelEmitter {
public:
    void emit(int dyPixels, int dxPixels, double pxPerNotch);
    void reset() { carryY_ = carryX_ = 0; }

private:
    double carryY_ = 0, carryX_ = 0;
};

// Everything the input thread injects goes out from two injector threads of its own, never from the thread that
// owns the hooks: `SendInput` runs every other low-level hook in the system for the injected event before it
// returns, and on this PC that took up to 266 ms per call while another program's hook thread was busy. The hook
// thread must never wait on that (Windows drops a hook that does not answer within 300 ms). The wheel thread
// coalesces units queued while it is busy (the distance survives, no stale frames pile up); the key thread keeps
// its batches in order (a mask precedes the release it masks) and retries a release Windows refused. Nothing here
// blocks the caller beyond an uncontended lock. The threads live for the process; nothing is destroyed at exit.

/// Starts both threads. Call once, early, from the thread that will queue (the input thread).
void startInjectors();
/// False when a thread could not be created: the callers then leave the physical events alone (fail open).
bool injectorReady();

/// Raw WHEEL_DELTA-scaled units, tagged, from the wheel thread.
void sendWheelUnits(int vertical, int horizontal);

/// The Start-menu mask: a key that means nothing (vk 0xE8, unassigned, no scancode), down and up, while the mouse's
/// Win is held, so that Windows, which saw that Win make, does not open the Start menu on its break. Returns the
/// request's sequence number; `maskSentSequence()` reaches it once the keys are out.
std::uint32_t queueMask();
std::uint32_t maskSentSequence();
/// Masks and releases that SendInput refused even after retries.
std::uint32_t maskFailures();
/// A tagged release of Win `vk` / `scan`, queued behind whatever is pending (a mask, normally): the fallback when the
/// physical release could not wait for its mask and was swallowed. Retried on failure.
void queueWinRelease(WORD vk, WORD scan);

/// The hook probe: one release of the unused mask key carrying kProbeTag, from the key thread.
/// Dropped unsent if it waited more than a second. `probeSentQpc()` is when the last one went out (0 before any),
/// `probeSendOk()` whether Windows took it.
void queueHookProbe();
long long probeSentQpc();
bool probeSendOk();

void sendKeyChord(std::initializer_list<WORD> vks);  // down in order, up in reverse, tagged (main thread)
void sendKey(WORD vk, bool down);
bool sendMouseButton(DWORD downFlag, DWORD upFlag, bool down);  // MOUSEEVENTF_*DOWN / *UP, at the current position; false if SendInput was blocked
void sendMediaKey(WORD vk);  // VK_VOLUME_*, VK_MEDIA_*

}  // namespace ixpat::win
