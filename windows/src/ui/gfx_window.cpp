#include "ui/gfx_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace ixpat::ui {
namespace {
constexpr UINT_PTR kAnimTimer = 1;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmCaptionColor = 35;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmBackdropMica = 2;  // DWMSBT_MAINWINDOW
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;
constexpr UINT kDwmColorizationChanged = 0x0320;  // WM_DWMCOLORIZATIONCOLORCHANGED
}  // namespace

GfxWindow::~GfxWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK GfxWindow::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* created = reinterpret_cast<GfxWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        if (created) created->hwnd_ = h;  // creation-time messages (WM_NCCALCSIZE, WM_CREATE, WM_SIZE) need the real handle
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* self = reinterpret_cast<GfxWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

bool GfxWindow::create(HINSTANCE inst, const wchar_t* className, const wchar_t* title, int widthDip, int heightDip,
                       int minWidthDip, int minHeightDip, HICON icon, bool resizable) {
    if (hwnd_) return true;
    inst_ = inst;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &GfxWindow::wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    RegisterClassW(&wc);
    const DWORD style = resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    // Size for the primary monitor's DPI first; WM_DPICHANGED corrects it when it lands elsewhere.
    const UINT dpi = GetDpiForSystem();
    dpi_ = static_cast<float>(dpi);
    RECT r{0, 0, MulDiv(widthDip, static_cast<int>(dpi), 96), MulDiv(heightDip, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    minW_ = minWidthDip;
    minH_ = minHeightDip;
    hwnd_ = CreateWindowExW(0, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    RECT c{};
    GetClientRect(hwnd_, &c);
    clientW_ = c.right;
    clientH_ = c.bottom;
    // Mica: the frame extends over the whole client, so pixels left transparent show the backdrop.
    MARGINS whole{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &whole);
    const DWORD backdrop = kDwmBackdropMica;
    micaOn_ = SUCCEEDED(DwmSetWindowAttribute(hwnd_, kDwmSystemBackdropType, &backdrop, sizeof backdrop));
    if (!micaOn_) {
        MARGINS none{};
        DwmExtendFrameIntoClientArea(hwnd_, &none);
    }
    applyFrame();
    return true;
}

void GfxWindow::applyFrame() {
    if (!hwnd_) return;
    const BOOL dark = palette_.isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof dark);
    // With Mica the title bar is the backdrop itself; without it, the base colour so the caption matches the client.
    const COLORREF caption = micaOn_ ? kDwmColorDefault : RGB((palette_.base >> 16) & 0xFF, (palette_.base >> 8) & 0xFF, palette_.base & 0xFF);
    DwmSetWindowAttribute(hwnd_, kDwmCaptionColor, &caption, sizeof caption);
}

void GfxWindow::setPalette(const Palette& p) {
    palette_ = p;
    applyFrame();
    invalidate();
}

void GfxWindow::show() {
    if (!hwnd_) return;
    if (!IsWindowVisible(hwnd_)) {
        // Centre on the monitor with the pointer.
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        RECT wr{};
        GetWindowRect(hwnd_, &wr);
        if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) {
            const int w = wr.right - wr.left, h = wr.bottom - wr.top;
            SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    invalidate();
}

void GfxWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void GfxWindow::close() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void GfxWindow::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void GfxWindow::animate(bool on) {
    if (!hwnd_ || on == animating_) return;
    animating_ = on;
    if (on) SetTimer(hwnd_, kAnimTimer, 16, nullptr);
    else KillTimer(hwnd_, kAnimTimer);
}

void GfxWindow::ensureTarget() {
    if (target_ || !hwnd_) return;
    auto* factory = Gfx::d2d();
    if (!factory) return;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, micaOn_ ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(static_cast<UINT32>(std::max(1, clientW_)), static_cast<UINT32>(std::max(1, clientH_))));
    factory->CreateHwndRenderTarget(&props, &hp, target_.put());
}

void GfxWindow::paint() {
    ensureTarget();
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (target_) {
        UiInput none;
        build(nullptr, none);  // a layout pass first: card backgrounds are drawn with the heights it measures
        target_->BeginDraw();
        target_->SetDpi(dpi_, dpi_);
        target_->Clear(micaOn_ ? D2D1::ColorF(0, 0, 0, 0) : color(palette_.base));
        build(target_.get(), none);
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.reset();
    }
    EndPaint(hwnd_, &ps);
}

void GfxWindow::dispatch(const UiInput& in) {
    build(nullptr, in);
    if (consumeRerun()) {
        UiInput none;
        build(nullptr, none);
    }
    invalidate();
}

LRESULT GfxWindow::onMessage(UINT, WPARAM, LPARAM, bool& handled) {
    handled = false;
    return 0;
}

LRESULT GfxWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    bool handled = false;
    const LRESULT r = onMessage(msg, wp, lp, handled);
    if (handled) return r;
    const float s = dpiScale();
    switch (msg) {
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            clientW_ = LOWORD(lp);
            clientH_ = HIWORD(lp);
            if (target_) target_->Resize(D2D1::SizeU(std::max<UINT32>(1, clientW_), std::max<UINT32>(1, clientH_)));
            invalidate();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
            RECT rc{0, 0, MulDiv(minW_, static_cast<int>(dpi), 96), MulDiv(minH_, static_cast<int>(dpi), 96)};
            AdjustWindowRectExForDpi(&rc, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE, 0, dpi);
            mm->ptMinTrackSize.x = rc.right - rc.left;
            mm->ptMinTrackSize.y = rc.bottom - rc.top;
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = static_cast<float>(HIWORD(wp));
            const RECT* nr = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
            target_.reset();
            invalidate();
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&t);
                tracking_ = true;
            }
            UiInput in;
            in.kind = UiInput::Kind::move;
            in.x = GET_X_LPARAM(lp) / s;
            in.y = GET_Y_LPARAM(lp) / s;
            in.leftDown = leftDown_;
            dispatch(in);
            return 0;
        }
        case WM_MOUSELEAVE: {
            tracking_ = false;
            UiInput in;
            in.kind = UiInput::Kind::leave;
            in.x = in.y = -1;
            dispatch(in);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: {
            SetFocus(hwnd_);
            SetCapture(hwnd_);
            leftDown_ = msg != WM_RBUTTONDOWN;
            UiInput in;
            in.kind = UiInput::Kind::down;
            in.x = GET_X_LPARAM(lp) / s;
            in.y = GET_Y_LPARAM(lp) / s;
            in.leftDown = leftDown_;
            in.rightButton = msg == WM_RBUTTONDOWN;
            dispatch(in);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            leftDown_ = false;  // before ReleaseCapture: its WM_CAPTURECHANGED must not synthesise a second release
            ReleaseCapture();
            UiInput in;
            in.kind = UiInput::Kind::up;
            in.x = GET_X_LPARAM(lp) / s;
            in.y = GET_Y_LPARAM(lp) / s;
            in.rightButton = msg == WM_RBUTTONUP;
            dispatch(in);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            UiInput in;
            in.kind = UiInput::Kind::wheel;
            in.x = pt.x / s;
            in.y = pt.y / s;
            in.wheel = GET_WHEEL_DELTA_WPARAM(wp);
            dispatch(in);
            return 0;
        }
        case WM_KEYDOWN: {
            UiInput in;
            in.kind = UiInput::Kind::key;
            in.key = static_cast<int>(wp);
            dispatch(in);
            return 0;
        }
        case WM_CHAR: {
            UiInput in;
            in.kind = UiInput::Kind::chr;
            in.chr = static_cast<wchar_t>(wp);
            dispatch(in);
            return 0;
        }
        case WM_TIMER:
            if (wp == kAnimTimer) {
                onTimer();
                invalidate();
                return 0;
            }
            break;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case kDwmColorizationChanged:
            onSystemThemeChanged();
            invalidate();
            break;
        case WM_CLOSE:
            hide();
            onClosed();
            return 0;
        case WM_CAPTURECHANGED:
            if (leftDown_ && reinterpret_cast<HWND>(lp) != hwnd_) {  // capture taken away mid-drag: end the drag
                leftDown_ = false;
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                UiInput in;
                in.kind = UiInput::Kind::up;
                in.x = pt.x / s;
                in.y = pt.y / s;
                dispatch(in);
            }
            return 0;
        case WM_DESTROY:
            target_.reset();
            return 0;
        case WM_NCDESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace ixpat::ui
