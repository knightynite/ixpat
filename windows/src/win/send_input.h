#pragma once
#include <windows.h>

#include <cstdint>
#include <initializer_list>

namespace ixpat::win {

/// Marks everything we synthesise (dwExtraInfo), so the hooks let it through untouched.
constexpr ULONG_PTR kOwnTag = 0x49585054;  // "IXPT"
inline bool isOurs(ULONG_PTR extraInfo) { return extraInfo == kOwnTag; }

/// Pixels -> WHEEL_DELTA units with sub-unit carry, so apps that only scroll by lines still get the
/// right total and apps with smooth scrolling get the fine stream.
class WheelEmitter {
public:
    void emit(int dyPixels, int dxPixels, double pxPerNotch);
    void reset() { carryY_ = carryX_ = 0; }

private:
    double carryY_ = 0, carryX_ = 0;
};

void sendWheelUnits(int vertical, int horizontal);  // raw WHEEL_DELTA-scaled units, tagged
void sendKeyChord(std::initializer_list<WORD> vks);  // down in order, up in reverse, tagged
void sendKey(WORD vk, bool down);
/// A key that means nothing (vk 0xFF, no scancode), pressed and released. Sent while the mouse's Win is down,
/// so that Windows, which saw that Win make, does not open the Start menu on its break.
void sendMaskKey();
bool sendMouseButton(DWORD downFlag, DWORD upFlag, bool down);  // MOUSEEVENTF_*DOWN / *UP, at the current position; false if SendInput was blocked
void sendMediaKey(WORD vk);  // VK_VOLUME_*, VK_MEDIA_*

}  // namespace ixpat::win
