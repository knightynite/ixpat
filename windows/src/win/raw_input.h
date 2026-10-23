#pragma once
#include <windows.h>

#include <cstdint>
#include <optional>

namespace ixpat::win {

enum class RawKind : std::uint8_t { move, button, wheel, hwheel, key, consumer, other };

/// One decoded WM_INPUT, with the device identity resolved.
struct RawEvent {
    RawKind kind = RawKind::other;
    bool surfaceMouse = false;  // the Surface Mouse (any of its collections), or any device in IXPAT_ANY_MOUSE mode
    bool injected = false;      // hDevice == NULL: SendInput (ours or another program's)
    std::int64_t qpc = 0;
    std::int32_t dx = 0, dy = 0;       // move
    std::uint16_t buttonFlags = 0;     // button: RI_MOUSE_* down/up flags
    std::int16_t wheel = 0;            // wheel / hwheel: signed delta (120 per notch)
    std::uint16_t vk = 0, scan = 0;    // key
    bool keyDown = false;
    std::uint16_t consumerUsage = 0;   // consumer: raw usage when decodable
};

/// Raw Input registration and decoding (the device side of the input path, like the Mac's IOHID reports).
class RawInput {
public:
    /// Mouse, keyboard and consumer collections with RIDEV_INPUTSINK on a message-only window.
    bool registerOn(HWND messageWindow);
    void unregister();
    std::optional<RawEvent> decode(HRAWINPUT handle);
    /// Development mode: every pointing device counts as the Surface Mouse.
    bool anyMouse = false;

private:
    bool isSurface(HANDLE device);
    struct Known {
        HANDLE device = nullptr;
        bool surface = false;
    };
    Known devices_[32]{};  // fixed: resolving a device never allocates (the hooks drain WM_INPUT)
    int deviceCount_ = 0;
    std::uint64_t buffer_[128]{};  // 1 KB, naturally aligned for RAWINPUT
};

bool isSurfaceMouseDevicePath(const wchar_t* path, std::size_t length);

}  // namespace ixpat::win
