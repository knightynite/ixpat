#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/actions.h"
#include "ui/d2d.h"

namespace ixpat::ui {

/// The Action Ring (RadialMenuController + RadialMenuView): a ring of chambers that opens under the
/// pointer. Sweep toward a chamber (a detent on each crossing), then release the action button (hold
/// mode) or click (toggle mode) to run it. The window is layered, topmost, non-activating and
/// click-through; the pointer is tracked by polling, so no click ever lands on the ring itself.
/// Main thread only.
class RingWindow {
public:
    std::function<void()> onDetent;
    std::function<void()> onOpen;
    std::function<void(const RingItem&)> onCommit;
    std::function<void(bool open, bool holdMode)> onStateChanged;
    std::function<void(const std::string&)> onLog;  // diagnostics (debug.log)

    void create(HINSTANCE inst);
    void destroy();
    void open(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex);
    void toggle(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex);
    void close(bool commit);
    /// Hold mode: the action button was released.
    void holdEnded();
    bool isOpen() const { return open_; }
    bool holdMode() const { return holdMode_; }
    /// 90 Hz while open (driven by the window's own timer).
    void poll();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void draw();
    void ensureFormats();

    static constexpr UINT_PTR kPollTimer = 1;
    HWND hwnd_ = nullptr;
    LayeredSurface surface_;
    std::vector<RingItem> items_;
    bool open_ = false, holdMode_ = false;
    std::optional<int> selected_;
    std::optional<int> drawnSelected_;
    POINT origin_{};
    double scale_ = 1;
    int sizePx_ = 360;
    std::uint32_t accent_ = 0x0A84FF;
    ULONGLONG openedAt_ = 0;
    std::uint8_t drawnAlpha_ = 0;
    Com<IDWriteTextFormat> hubFormat_, glyphFormat_, letterFormat_;
    float formatScale_ = 0;
};

}  // namespace ixpat::ui
