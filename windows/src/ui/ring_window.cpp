#include "ui/ring_window.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "core/ring.h"
#include "ui/glyphs.h"

namespace ixpat::ui {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kDisc = 0x151518;
constexpr float kDiscAlpha = 0.86f;
constexpr ULONGLONG kFadeMs = 120;
const wchar_t* kClass = L"iXpat.Ring";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

D2D1_POINT_2F polar(D2D1_POINT_2F c, double r, double a) {
    return D2D1::Point2F(static_cast<float>(c.x + r * std::cos(a)), static_cast<float>(c.y + r * std::sin(a)));
}

/// One annular segment of the wheel; chamber `index` is centred at 12 o'clock + index * slice.
Com<ID2D1PathGeometry> segment(D2D1_POINT_2F c, int index, int count, double inner, double outer) {
    Com<ID2D1PathGeometry> g;
    if (FAILED(Gfx::d2d()->CreatePathGeometry(g.put()))) return g;
    Com<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(sink.put()))) return g;
    const double slice = 2 * kPi / count;
    const double a0 = index * slice - kPi / 2 - slice / 2, a1 = a0 + slice;
    const D2D1_ARC_SIZE size = slice > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->BeginFigure(polar(c, outer, a0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(polar(c, outer, a1), D2D1::SizeF(static_cast<float>(outer), static_cast<float>(outer)), 0,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, size));
    sink->AddLine(polar(c, inner, a1));
    sink->AddArc(D2D1::ArcSegment(polar(c, inner, a0), D2D1::SizeF(static_cast<float>(inner), static_cast<float>(inner)), 0,
                                  D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, size));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

}  // namespace

LRESULT CALLBACK RingWindow::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return TRUE;
    }
    auto* self = reinterpret_cast<RingWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && msg == WM_TIMER && wp == kPollTimer) {
        self->poll();
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, msg, wp, lp);
}

void RingWindow::create(HINSTANCE inst) {
    if (hwnd_) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &RingWindow::wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass,
                            L"", WS_POPUP, 0, 0, kRingSize, kRingSize, nullptr, nullptr, inst, this);
    if (!hwnd_ && onLog) onLog("ring: CreateWindowEx failed, error " + std::to_string(GetLastError()));
    if (onLog && !Gfx::d2d()) onLog("ring: Direct2D factory unavailable");
    if (onLog && !Gfx::dwrite()) onLog("ring: DirectWrite factory unavailable");
}

void RingWindow::destroy() {
    if (open_) close(false);
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void RingWindow::ensureFormats() {
    const float scale = static_cast<float>(scale_);
    if (hubFormat_ && formatScale_ == scale) return;
    auto* dw = Gfx::dwrite();
    if (!dw) return;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         13.0f * scale * 96.0f / 72.0f, L"en-us", hubFormat_.put());
    dw->CreateTextFormat(iconFontFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         22.0f * scale, L"en-us", glyphFormat_.put());
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         20.0f * scale, L"en-us", letterFormat_.put());
    for (IDWriteTextFormat* f : {hubFormat_.get(), glyphFormat_.get(), letterFormat_.get()}) {
        if (!f) continue;
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    if (hubFormat_) {
        hubFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        hubFormat_->SetTrimming(&trim, nullptr);
    }
    formatScale_ = scale;
}

void RingWindow::open(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex) {
    if (open_ || items.empty() || !hwnd_) return;
    items_ = items;
    holdMode_ = holdMode;
    accent_ = accentHex & 0xFFFFFF;
    selected_.reset();
    drawnSelected_.reset();
    GetCursorPos(&origin_);
    // Size for the monitor under the pointer.
    UINT dpiX = 96, dpiY = 96;
    if (HMONITOR mon = MonitorFromPoint(origin_, MONITOR_DEFAULTTONEAREST)) GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = std::max(0.5, dpiX / 96.0);
    sizePx_ = static_cast<int>(std::lround(kRingSize * scale_));
    if (!surface_.resize(sizePx_, sizePx_)) {
        if (onLog) onLog("ring: surface resize failed (" + std::to_string(sizePx_) + " px)");
        return;
    }
    ensureFormats();
    if (onLog) onLog("ring: open at " + std::to_string(origin_.x) + "," + std::to_string(origin_.y) + " size " + std::to_string(sizePx_) + " scale " + std::to_string(scale_));
    open_ = true;
    openedAt_ = GetTickCount64();
    drawnAlpha_ = 0;
    SetWindowPos(hwnd_, HWND_TOPMOST, origin_.x - sizePx_ / 2, origin_.y - sizePx_ / 2, sizePx_, sizePx_, SWP_NOACTIVATE);
    draw();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetTimer(hwnd_, kPollTimer, 11, nullptr);  // ~90 Hz
    if (onStateChanged) onStateChanged(true, holdMode_);
    if (onOpen) onOpen();
}

void RingWindow::toggle(const std::vector<RingItem>& items, bool holdMode, std::uint32_t accentHex) {
    if (open_) close(true);
    else open(items, holdMode, accentHex);
}

void RingWindow::holdEnded() {
    if (open_ && holdMode_) close(true);
}

void RingWindow::close(bool commit) {
    if (!open_) return;
    open_ = false;
    KillTimer(hwnd_, kPollTimer);
    std::optional<RingItem> chosen;
    if (commit) {  // the release can land between two polls: sample the pointer now
        POINT p{};
        if (GetCursorPos(&p)) selected_ = ringSelect((p.x - origin_.x) / scale_, (p.y - origin_.y) / scale_, static_cast<int>(items_.size()));
    }
    if (selected_ && *selected_ >= 0 && *selected_ < static_cast<int>(items_.size())) chosen = items_[*selected_];
    ShowWindow(hwnd_, SW_HIDE);
    selected_.reset();
    const bool wasHold = holdMode_;
    holdMode_ = false;
    if (onStateChanged) onStateChanged(false, wasHold);
    if (commit && chosen && onCommit) onCommit(*chosen);
}

void RingWindow::poll() {
    if (!open_) return;
    POINT p{};
    if (!GetCursorPos(&p)) return;
    const double dx = (p.x - origin_.x) / scale_, dy = (p.y - origin_.y) / scale_;
    const auto sel = ringSelect(dx, dy, static_cast<int>(items_.size()));
    if (sel != selected_) {
        selected_ = sel;
        if (sel && onDetent) onDetent();
    }
    const ULONGLONG elapsed = GetTickCount64() - openedAt_;
    const auto alpha = static_cast<std::uint8_t>(std::min<ULONGLONG>(255, elapsed * 255 / kFadeMs));
    if (sel != drawnSelected_ || alpha != drawnAlpha_) draw();
}

void RingWindow::draw() {
    if (!surface_.resize(sizePx_, sizePx_)) return;  // a no-op unless the device was lost
    ID2D1RenderTarget* rt = surface_.begin();
    if (!rt) return;
    const float S = static_cast<float>(sizePx_);
    const D2D1_POINT_2F c = D2D1::Point2F(S / 2, S / 2);
    const double outer = 0.46 * S, inner = 0.24 * S, mid = (outer + inner) / 2;
    const int n = std::max<int>(1, static_cast<int>(items_.size()));
    Com<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color(kDisc, kDiscAlpha), brush.put());
    if (!brush) {
        surface_.end(hwnd_, {origin_.x - sizePx_ / 2, origin_.y - sizePx_ / 2}, 0);
        return;
    }
    // Disc
    rt->FillEllipse(D2D1::Ellipse(c, static_cast<float>(outer + 3), static_cast<float>(outer + 3)), brush.get());
    // Wedges
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        auto g = segment(c, i, n, inner, outer);
        if (!g) continue;
        const bool on = selected_ && *selected_ == i;
        if (on) {
            brush->SetColor(color(accent_, 0.35f));
            rt->FillGeometry(g.get(), brush.get());  // a soft glow under the lit wedge
            brush->SetColor(color(accent_, 1.0f));
        } else {
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.06f));
        }
        rt->FillGeometry(g.get(), brush.get());
        brush->SetColor(color(kDisc, 1.0f));
        rt->DrawGeometry(g.get(), brush.get(), 3.0f * static_cast<float>(scale_));
    }
    // Icons (glyph, or the first letter of the title)
    const float box = static_cast<float>(32 * scale_);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const double a = i * (2 * kPi / n) - kPi / 2;
        const D2D1_POINT_2F p = polar(c, mid, a);
        const D2D1_RECT_F r = D2D1::RectF(p.x - box, p.y - box, p.x + box, p.y + box);
        const bool on = selected_ && *selected_ == i;
        brush->SetColor(on ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(1, 1, 1, 0.85f));
        const wchar_t glyph = glyphFor(items_[i].symbol);
        if (glyph && glyphFormat_) {
            rt->DrawTextW(&glyph, 1, glyphFormat_.get(), r, brush.get());
        } else if (letterFormat_) {
            std::wstring title = widen(items_[i].title);
            wchar_t letter = title.empty() ? L'?' : static_cast<wchar_t>(towupper(title[0]));
            rt->DrawTextW(&letter, 1, letterFormat_.get(), r, brush.get());
        }
    }
    // Hub: the highlighted action's name
    if (selected_ && *selected_ >= 0 && *selected_ < static_cast<int>(items_.size()) && hubFormat_) {
        std::wstring title = widen(items_[*selected_].title);
        const float hub = static_cast<float>(inner * 0.86);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), hubFormat_.get(),
                      D2D1::RectF(c.x - hub, c.y - hub, c.x + hub, c.y + hub), brush.get());
    }
    drawnSelected_ = selected_;
    const ULONGLONG elapsed = GetTickCount64() - openedAt_;
    drawnAlpha_ = static_cast<std::uint8_t>(std::min<ULONGLONG>(255, elapsed * 255 / kFadeMs));
    surface_.end(hwnd_, {origin_.x - sizePx_ / 2, origin_.y - sizePx_ / 2}, drawnAlpha_);
    if (surface_.lastError() && onLog) onLog("ring: UpdateLayeredWindow failed, error " + std::to_string(surface_.lastError()));
}

}  // namespace ixpat::ui
