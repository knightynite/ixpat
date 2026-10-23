#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>

#include <cstdint>

namespace ixpat::ui {

/// Minimal COM holder (no ATL/WRL dependency).
template <class T>
class Com {
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) {
            reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    ~Com() { reset(); }
    void reset() {
        if (p_) p_->Release();
        p_ = nullptr;
    }
    T** put() {
        reset();
        return &p_;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

/// Process-wide Direct2D / DirectWrite factories (main thread).
struct Gfx {
    static ID2D1Factory1* d2d();
    static IDWriteFactory* dwrite();
};

/// A per-pixel-alpha surface for a WS_EX_LAYERED window: a top-down 32-bit DIB with a Direct2D DC render
/// target bound to it; `end` pushes it to the window with UpdateLayeredWindow (premultiplied alpha).
class LayeredSurface {
public:
    LayeredSurface() = default;
    ~LayeredSurface();
    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;

    bool resize(int width, int height);
    ID2D1RenderTarget* begin();  // BeginDraw + Clear(transparent); nullptr when not ready
    void end(HWND hwnd, POINT screenPos, std::uint8_t alpha = 255);
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();
    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    Com<ID2D1DCRenderTarget> target_;
    int width_ = 0, height_ = 0;
};

inline D2D1_COLOR_F color(std::uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

}  // namespace ixpat::ui
