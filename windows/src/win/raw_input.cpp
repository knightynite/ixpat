#include "win/raw_input.h"

#include <hidusage.h>

#include <cwctype>
#include <iterator>
#include <string>

namespace ixpat::win {

bool isSurfaceMouseDevicePath(const wchar_t* path, std::size_t length) {
    // Case-insensitive substring search without allocating (called from the input thread).
    auto contains = [&](const wchar_t* needle) {
        const std::size_t n = wcslen(needle);
        if (length < n) return false;
        for (std::size_t i = 0; i + n <= length; ++i) {
            std::size_t k = 0;
            while (k < n && std::towlower(path[i + k]) == needle[k]) ++k;
            if (k == n) return true;
        }
        return false;
    };
    return contains(L"vid&02045e_pid&0880") || contains(L"vid_045e&pid_0880");
}

bool RawInput::registerOn(HWND hwnd) {
    RAWINPUTDEVICE rid[3]{};
    rid[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_INPUTSINK, hwnd};
    rid[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, RIDEV_INPUTSINK, hwnd};
    rid[2] = {HID_USAGE_PAGE_CONSUMER, 0x01, RIDEV_INPUTSINK, hwnd};
    return RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE)) != 0;
}

void RawInput::unregister() {
    RAWINPUTDEVICE rid[3]{};
    rid[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_REMOVE, nullptr};
    rid[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, RIDEV_REMOVE, nullptr};
    rid[2] = {HID_USAGE_PAGE_CONSUMER, 0x01, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE));
}

bool RawInput::isSurface(HANDLE device) {
    if (!device) return false;
    for (int i = 0; i < deviceCount_; ++i) {
        if (devices_[i].device == device) return devices_[i].surface;
    }
    bool surface = false;
    UINT size = 0;
    GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size);
    if (size && size < 1024) {
        wchar_t name[1024];
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name, &size) != static_cast<UINT>(-1)) {
            std::size_t len = wcsnlen(name, size);
            surface = isSurfaceMouseDevicePath(name, len);
        }
    }
    // A handle set that changes is rare (plug/unplug); when the table is full the oldest entry goes.
    if (deviceCount_ < static_cast<int>(std::size(devices_))) {
        devices_[deviceCount_++] = {device, surface};
    } else {
        for (int i = 1; i < deviceCount_; ++i) devices_[i - 1] = devices_[i];
        devices_[deviceCount_ - 1] = {device, surface};
    }
    return surface;
}

std::optional<RawEvent> RawInput::decode(HRAWINPUT handle) {
    UINT size = sizeof buffer_;
    const UINT got = GetRawInputData(handle, RID_INPUT, buffer_, &size, sizeof(RAWINPUTHEADER));
    if (got == static_cast<UINT>(-1) || got < sizeof(RAWINPUTHEADER)) return std::nullopt;
    const auto* ri = reinterpret_cast<const RAWINPUT*>(buffer_);
    RawEvent e;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    e.qpc = t.QuadPart;
    e.injected = ri->header.hDevice == nullptr;
    const bool surface = isSurface(ri->header.hDevice);
    switch (ri->header.dwType) {
        case RIM_TYPEMOUSE: {
            const RAWMOUSE& m = ri->data.mouse;
            e.surfaceMouse = surface || (anyMouse && !e.injected);
            if (m.usButtonFlags & RI_MOUSE_WHEEL) {
                e.kind = RawKind::wheel;
                e.wheel = static_cast<std::int16_t>(m.usButtonData);
            } else if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
                e.kind = RawKind::hwheel;
                e.wheel = static_cast<std::int16_t>(m.usButtonData);
            } else if (m.usButtonFlags & 0x03FF) {
                e.kind = RawKind::button;
                e.buttonFlags = static_cast<std::uint16_t>(m.usButtonFlags & 0x03FF);
            } else {
                e.kind = RawKind::move;
                e.dx = m.lLastX;
                e.dy = m.lLastY;
            }
            break;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& k = ri->data.keyboard;
            e.kind = RawKind::key;
            e.surfaceMouse = surface;  // never anyMouse: a real keyboard must not be treated as the chord source
            e.vk = k.VKey;
            e.scan = k.MakeCode;
            e.keyDown = (k.Flags & RI_KEY_BREAK) == 0;
            break;
        }
        case RIM_TYPEHID: {
            e.kind = RawKind::consumer;
            e.surfaceMouse = surface;
            const RAWHID& h = ri->data.hid;
            if (h.dwSizeHid >= 3 && h.dwCount >= 1) e.consumerUsage = static_cast<std::uint16_t>(h.bRawData[1] | (h.bRawData[2] << 8));
            break;
        }
        default:
            e.kind = RawKind::other;
            break;
    }
    return e;
}

}  // namespace ixpat::win
