#include "ui/d2d.h"

namespace ixpat::ui {

ID2D1Factory1* Gfx::d2d() {
    static ID2D1Factory1* factory = [] {
        ID2D1Factory1* f = nullptr;
        D2D1_FACTORY_OPTIONS opts{};
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, reinterpret_cast<void**>(&f));
        return f;
    }();
    return factory;
}

IDWriteFactory* Gfx::dwrite() {
    static IDWriteFactory* factory = [] {
        IDWriteFactory* f = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&f));
        return f;
    }();
    return factory;
}

LayeredSurface::~LayeredSurface() { release(); }

void LayeredSurface::release() {
    target_.reset();
    if (dc_) {
        if (oldBitmap_) SelectObject(dc_, oldBitmap_);
        DeleteDC(dc_);
    }
    if (dib_) DeleteObject(dib_);
    dc_ = nullptr;
    dib_ = nullptr;
    oldBitmap_ = nullptr;
    width_ = height_ = 0;
}

bool LayeredSurface::resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == width_ && height == height_ && target_) return true;
    release();
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_) return false;
    dc_ = CreateCompatibleDC(nullptr);
    oldBitmap_ = SelectObject(dc_, dib_);
    auto* factory = Gfx::d2d();
    if (!factory) return false;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    if (FAILED(factory->CreateDCRenderTarget(&props, target_.put()))) return false;
    RECT rc{0, 0, width, height};
    if (FAILED(target_->BindDC(dc_, &rc))) return false;
    width_ = width;
    height_ = height;
    return true;
}

ID2D1RenderTarget* LayeredSurface::begin() {
    if (!target_) return nullptr;
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    return target_.get();
}

void LayeredSurface::end(HWND hwnd, POINT screenPos, std::uint8_t alpha) {
    if (!target_) return;
    target_->EndDraw();
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT src{0, 0};
    SIZE size{width_, height_};
    if (!UpdateLayeredWindow(hwnd, nullptr, &screenPos, &size, dc_, &src, 0, &bf, ULW_ALPHA)) lastError_ = GetLastError();
    else lastError_ = 0;
}

}  // namespace ixpat::ui
