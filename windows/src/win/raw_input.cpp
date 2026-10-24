#include "win/raw_input.h"

#include <hidusage.h>

#include <cwctype>
#include <iterator>
#include <vector>

namespace ixpat::win {

bool isSurfaceMouseDevicePath(const wchar_t* path, std::size_t length) {
    // Case-insensitive substring search without allocating.
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
    rid[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd};
    rid[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd};
    rid[2] = {HID_USAGE_PAGE_CONSUMER, 0x01, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd};
    return RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE)) != 0;
}

void RawInput::unregister() {
    RAWINPUTDEVICE rid[3]{};
    rid[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_REMOVE, nullptr};
    rid[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, RIDEV_REMOVE, nullptr};
    rid[2] = {HID_USAGE_PAGE_CONSUMER, 0x01, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE));
}

bool RawInput::resolve(HANDLE device) {
    bool surface = false;
    UINT size = 0;
    GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size);
    if (size && size < 1024) {
        wchar_t name[1024]{};
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name, &size) != static_cast<UINT>(-1)) {
            surface = isSurfaceMouseDevicePath(name, wcsnlen(name, size));
        }
    }
    for (int i = 0; i < deviceCount_; ++i) {
        if (devices_[i].device == device) {
            devices_[i].surface = surface;
            return surface;
        }
    }
    if (deviceCount_ < static_cast<int>(std::size(devices_))) {
        devices_[deviceCount_++] = {device, surface};
    } else {  // full: the oldest entry goes
        constexpr int last = static_cast<int>(std::size(devices_)) - 1;
        for (int i = 0; i < last; ++i) devices_[i] = devices_[i + 1];
        devices_[last] = {device, surface};
    }
    return surface;
}

void RawInput::resolveAll() {
    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (!count) return;
    std::vector<RAWINPUTDEVICELIST> list(count);
    const UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == static_cast<UINT>(-1)) return;
    for (UINT i = 0; i < got; ++i) resolve(list[i].hDevice);
}

void RawInput::deviceChanged(WPARAM gidc, HANDLE device) {
    if (gidc == GIDC_ARRIVAL) {
        resolve(device);
    } else if (gidc == GIDC_REMOVAL) {
        for (int i = 0; i < deviceCount_; ++i) {
            if (devices_[i].device != device) continue;
            for (int k = i + 1; k < deviceCount_; ++k) devices_[k - 1] = devices_[k];
            --deviceCount_;
            break;
        }
    }
}

bool RawInput::isSurface(HANDLE device, bool allowResolve, bool& known) {
    known = true;
    if (!device) return false;
    for (int i = 0; i < deviceCount_; ++i) {
        if (devices_[i].device == device) return devices_[i].surface;
    }
    if (!allowResolve) {
        known = false;  // inside a hook: never query the device name here
        return false;
    }
    return resolve(device);
}

int RawInput::decode(HRAWINPUT handle, RawEvent out[3], bool allowResolve) {
    UINT size = sizeof buffer_;
    const UINT got = GetRawInputData(handle, RID_INPUT, buffer_, &size, sizeof(RAWINPUTHEADER));
    if (got == static_cast<UINT>(-1) || got < sizeof(RAWINPUTHEADER)) return 0;
    const auto* ri = reinterpret_cast<const RAWINPUT*>(buffer_);
    RawEvent base;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    base.qpc = t.QuadPart;
    base.injected = ri->header.hDevice == nullptr;
    bool known = true;
    const bool surface = isSurface(ri->header.hDevice, allowResolve, known);
    base.unknownDevice = !known;
    int n = 0;
    switch (ri->header.dwType) {
        case RIM_TYPEMOUSE: {
            const RAWMOUSE& m = ri->data.mouse;
            base.surfaceMouse = surface || (anyMouse_ && !base.injected);
            // One packet can carry buttons, a wheel and motion at once: one component each.
            if (m.usButtonFlags & 0x03FF) {
                out[n] = base;
                out[n].kind = RawKind::button;
                out[n].buttonFlags = static_cast<std::uint16_t>(m.usButtonFlags & 0x03FF);
                ++n;
            }
            if (m.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL)) {
                out[n] = base;
                out[n].kind = (m.usButtonFlags & RI_MOUSE_WHEEL) ? RawKind::wheel : RawKind::hwheel;
                out[n].wheel = static_cast<std::int16_t>(m.usButtonData);
                ++n;
            }
            if (m.lLastX || m.lLastY) {
                out[n] = base;
                out[n].kind = RawKind::move;
                out[n].dx = m.lLastX;
                out[n].dy = m.lLastY;
                ++n;
            }
            return n;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& k = ri->data.keyboard;
            out[0] = base;
            out[0].kind = RawKind::key;
            out[0].surfaceMouse = surface;  // never anyMouse: a real keyboard must not be treated as the chord source
            out[0].vk = k.VKey;
            out[0].scan = k.MakeCode;
            out[0].keyDown = (k.Flags & RI_KEY_BREAK) == 0;
            out[0].keyE0 = (k.Flags & RI_KEY_E0) != 0;
            return 1;
        }
        case RIM_TYPEHID: {
            out[0] = base;
            out[0].kind = RawKind::consumer;
            out[0].surfaceMouse = surface;
            const RAWHID& h = ri->data.hid;
            if (h.dwSizeHid >= 3 && h.dwCount >= 1) out[0].consumerUsage = static_cast<std::uint16_t>(h.bRawData[1] | (h.bRawData[2] << 8));
            return 1;
        }
        default:
            return 0;
    }
}

}  // namespace ixpat::win
