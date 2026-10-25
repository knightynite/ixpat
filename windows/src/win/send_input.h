#pragma once
#include <windows.h>

#include <cstdint>
#include <initializer_list>

namespace ixpat::win {

/// Marks everything we synthesise (dwExtraInfo), so the hooks let it through untouched.
constexpr ULONG_PTR kOwnTag = 0x49585054;    // "IXPT"
constexpr ULONG_PTR kProbeTag = 0x49585050;  // "IXPP": the hook probes, so the hooks can tell they saw one
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

/// Raw WHEEL_DELTA-scaled units, tagged, sent by the injector thread. SendInput can block for hundreds of
/// milliseconds while Windows runs every other low-level hook for the injected event, and the thread that owns
/// our hooks must never wait on that (Windows drops a hook that does not answer within 300 ms). Units queued
/// while the injector is busy coalesce, so the distance survives and no stale frames pile up behind a slow call.
void sendWheelUnits(int vertical, int horizontal);
void sendKeyChord(std::initializer_list<WORD> vks);  // down in order, up in reverse, tagged
void sendKey(WORD vk, bool down);
/// A key that means nothing (vk 0xE8, unassigned, no scancode), pressed and released. Sent while the mouse's
/// Win is down, so that Windows, which saw that Win make, does not open the Start menu on its break. False when
/// SendInput did not take both events.
bool sendMaskKey();
/// The mask key (down, up) and then a Win release for `scan` (extended, tagged), queued for the injector thread in
/// that order. The hook swallows the physical Win release and hands it over here, so the thread that owns the
/// hooks never waits on SendInput while the mask goes first.
void queueMaskThenWinUp(WORD vk, WORD scan);
/// One release of the mask key and one empty mouse move, tagged, from the injector thread: both hooks see them if
/// they are alive, nothing else notices.
void queueHookProbe();
bool sendMouseButton(DWORD downFlag, DWORD upFlag, bool down);  // MOUSEEVENTF_*DOWN / *UP, at the current position; false if SendInput was blocked
void sendMediaKey(WORD vk);  // VK_VOLUME_*, VK_MEDIA_*

}  // namespace ixpat::win
