#include "win/send_input.h"

#include <cmath>
#include <vector>

namespace ixpat::win {

void WheelEmitter::emit(int dyPixels, int dxPixels, double pxPerNotch) {
    if (pxPerNotch <= 0) pxPerNotch = 60;
    auto convert = [&](int px, double& carry) {
        const double units = px * (WHEEL_DELTA / pxPerNotch) + carry;
        const double whole = std::trunc(units);
        carry = units - whole;
        return static_cast<int>(whole);
    };
    const int v = convert(dyPixels, carryY_);
    const int h = convert(dxPixels, carryX_);
    if (v || h) sendWheelUnits(v, h);
}

void sendWheelUnits(int vertical, int horizontal) {
    INPUT in[2]{};
    UINT n = 0;
    if (vertical) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.mouseData = static_cast<DWORD>(vertical);
        in[n].mi.dwFlags = MOUSEEVENTF_WHEEL;
        in[n].mi.dwExtraInfo = kOwnTag;
        ++n;
    }
    if (horizontal) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.mouseData = static_cast<DWORD>(horizontal);
        in[n].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in[n].mi.dwExtraInfo = kOwnTag;
        ++n;
    }
    if (n) SendInput(n, in, sizeof(INPUT));
}

void sendKey(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    switch (vk) {  // extended keys need the flag or some apps see the wrong key
        case VK_LWIN: case VK_RWIN: case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT: case VK_UP:
        case VK_DOWN: case VK_SNAPSHOT: case VK_DIVIDE: case VK_NUMLOCK: case VK_MEDIA_NEXT_TRACK:
        case VK_MEDIA_PREV_TRACK: case VK_MEDIA_PLAY_PAUSE: case VK_MEDIA_STOP: case VK_VOLUME_UP:
        case VK_VOLUME_DOWN: case VK_VOLUME_MUTE: case VK_APPS:
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
        default:
            break;
    }
    in.ki.dwExtraInfo = kOwnTag;
    SendInput(1, &in, sizeof in);
}

bool sendMaskKey() {
    INPUT in[2]{};
    for (int i = 0; i < 2; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = 0xE8;
        in[i].ki.dwFlags = i ? KEYEVENTF_KEYUP : 0;
        in[i].ki.dwExtraInfo = kOwnTag;
    }
    return SendInput(2, in, sizeof(INPUT)) == 2;
}

void sendKeyChord(std::initializer_list<WORD> vks) {
    std::vector<INPUT> in;
    in.reserve(vks.size() * 2);
    auto add = [&](WORD vk, bool down) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        i.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
            vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_INSERT || vk == VK_DELETE ||
            vk == VK_SNAPSHOT || vk == VK_APPS || vk == VK_RCONTROL || vk == VK_RMENU) {
            i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        i.ki.dwExtraInfo = kOwnTag;
        in.push_back(i);
    };
    for (WORD vk : vks) add(vk, true);
    for (auto it = std::rbegin(vks); it != std::rend(vks); ++it) add(*it, false);
    if (!in.empty()) SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}

bool sendMouseButton(DWORD downFlag, DWORD upFlag, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? downFlag : upFlag;
    in.mi.dwExtraInfo = kOwnTag;
    return SendInput(1, &in, sizeof in) == 1;
}

void sendMediaKey(WORD vk) {
    sendKey(vk, true);
    sendKey(vk, false);
}

}  // namespace ixpat::win
