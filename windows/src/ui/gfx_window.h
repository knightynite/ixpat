#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include "ui/d2d.h"
#include "ui/theme.h"

namespace ixpat::ui {

/// What one input event looks like to the immediate-mode toolkit, in DIPs.
struct UiInput {
    enum class Kind { none, move, down, up, wheel, key, chr, leave } kind = Kind::none;
    float x = 0, y = 0;
    int wheel = 0;    // WHEEL_DELTA units, positive = up
    int key = 0;      // virtual key for Kind::key
    wchar_t chr = 0;  // for Kind::chr
    bool leftDown = false;
    bool rightButton = false;
};

/// A framed Direct2D window with the Windows 11 shell: Mica through the frame and the client area (the client is
/// painted with per-pixel alpha, so the backdrop shows wherever nothing opaque is drawn), the system title bar in
/// the theme's tone, per-monitor DPI, paint on invalidate, input events handed to `build`, a 60 Hz animation timer
/// while `animate(true)`. Without Mica (Windows 10, or DWM saying no) the client is cleared to the base colour.
class GfxWindow {
public:
    GfxWindow() = default;
    virtual ~GfxWindow();
    GfxWindow(const GfxWindow&) = delete;
    GfxWindow& operator=(const GfxWindow&) = delete;

    bool create(HINSTANCE inst, const wchar_t* className, const wchar_t* title, int widthDip, int heightDip, int minWidthDip,
                int minHeightDip, HICON icon, bool resizable = true);
    void show();
    void hide();
    void close();
    bool isVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    HWND hwnd() const { return hwnd_; }
    float dpiScale() const { return dpi_ / 96.0f; }
    void invalidate();
    void animate(bool on);
    void setPalette(const Palette& p);
    const Palette& palette() const { return palette_; }
    bool micaOn() const { return micaOn_; }
    /// Client size in DIPs.
    float widthDip() const { return clientW_ / dpiScale(); }
    float heightDip() const { return clientH_ / dpiScale(); }

protected:
    /// Called for painting (rt != nullptr) and for each input event (rt == nullptr): same layout both times.
    virtual void build(ID2D1RenderTarget* rt, const UiInput& input) = 0;
    virtual void onClosed() {}
    virtual void onTimer() {}
    virtual void onSystemThemeChanged() {}  // the accent or the light / dark preference changed
    virtual LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled);  // extra messages (native edit, etc.)
    /// After an input pass: true when the toolkit wants one more empty input pass (a popup selection waits).
    virtual bool consumeRerun() { return false; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    void ensureTarget();
    void dispatch(const UiInput& in);
    void applyFrame();

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    Com<ID2D1HwndRenderTarget> target_;
    Palette palette_{};
    float dpi_ = 96;
    int clientW_ = 0, clientH_ = 0;
    int minW_ = 0, minH_ = 0;
    bool animating_ = false;
    bool tracking_ = false;
    bool leftDown_ = false;
    bool micaOn_ = false;
};

}  // namespace ixpat::ui
