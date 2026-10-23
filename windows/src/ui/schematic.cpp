#include "ui/schematic.h"

#include <algorithm>
#include <cmath>

namespace ixpat::ui {
namespace {

/// A tall pebble, slightly narrower at the front, big-radius corners, one-piece top shell.
/// Proportions from Microsoft's spec: 4.3 in long, 2.48 in wide. Normalised bezier path from MouseBodyShape.
Com<ID2D1PathGeometry> bodyPath(const Rect& r) {
    Com<ID2D1PathGeometry> g;
    if (FAILED(Gfx::d2d()->CreatePathGeometry(g.put()))) return g;
    Com<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(sink.put()))) return g;
    auto pt = [&](float x, float y) { return D2D1::Point2F(r.x + r.w * x, r.y + r.h * y); };
    sink->BeginFigure(pt(0.5f, 0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(pt(0.80f, 0), pt(0.97f, 0.06f), pt(0.97f, 0.22f)));      // front-right corner
    sink->AddBezier(D2D1::BezierSegment(pt(0.99f, 0.40f), pt(1.0f, 0.55f), pt(1.0f, 0.70f)));     // right flank
    sink->AddBezier(D2D1::BezierSegment(pt(1.0f, 0.92f), pt(0.78f, 1.0f), pt(0.5f, 1.0f)));       // rear-right corner
    sink->AddBezier(D2D1::BezierSegment(pt(0.22f, 1.0f), pt(0.0f, 0.92f), pt(0.0f, 0.70f)));      // rear-left corner
    sink->AddBezier(D2D1::BezierSegment(pt(0.0f, 0.55f), pt(0.01f, 0.40f), pt(0.03f, 0.22f)));    // left flank
    sink->AddBezier(D2D1::BezierSegment(pt(0.03f, 0.06f), pt(0.20f, 0), pt(0.5f, 0)));            // front-left corner
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

}  // namespace

void drawSchematic(Frame& f, const Rect& area, const SchematicState& s) {
    ID2D1RenderTarget* rt = f.rt();
    if (!rt) return;
    const Palette& pal = f.pal();
    // Size: the Mac fits w = min(56% of width, height / 1.72).
    const float w = std::min(area.w * 0.56f, area.h / 1.72f), h = w * 1.72f;
    const float dx = std::clamp(s.dx, -6.0f, 6.0f) * 0.6f, dy = std::clamp(s.dy, -6.0f, 6.0f) * 0.6f;
    const Rect body{area.x + (area.w - w) / 2 + dx, area.y + (area.h - h) / 2 + dy, w, h};
    const float alpha = s.connected ? 1.0f : 0.45f;
    Com<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush.put());
    if (!brush) return;

    // Pulse rings ripple out of the body (0.6 s ease-out from 0.5x to 2.1x of 0.9w, fading).
    for (float age : s.pulseAges) {
        const float t = std::clamp(age / 0.6f, 0.0f, 1.0f);
        const float e = 1 - (1 - t) * (1 - t);  // ease-out
        const float radius = w * 0.9f / 2 * (0.5f + 1.6f * e);
        brush->SetColor(color(pal.accent, 0.85f * (1 - e)));
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(body.x + w / 2, body.y + h / 2 + h * 0.1f), radius, radius), brush.get(), 2.5f);
    }

    auto shape = bodyPath(body);
    if (!shape) return;
    // Shadow
    brush->SetColor(D2D1::ColorF(0, 0, 0, (pal.isDark ? 0.5f : 0.22f) * alpha));
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0, 8) * old);
    rt->FillGeometry(shape.get(), brush.get());
    rt->SetTransform(old);
    // Shell
    brush->SetColor(color(pal.mouseShell, alpha));
    rt->FillGeometry(shape.get(), brush.get());
    // Button fills: the front 48% of the shell, split left / right, lit from the mouse's own reports.
    const float seamY = 0.48f;
    Com<ID2D1Layer> layer;
    rt->CreateLayer(nullptr, layer.put());
    if (layer) {
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.get()), layer.get());
        if (s.buttons & 0x01) {
            brush->SetColor(color(pal.accent, 0.55f * alpha));
            rt->FillRectangle(D2D1::RectF(body.x, body.y, body.x + w / 2, body.y + h * seamY), brush.get());
        }
        if (s.buttons & 0x02) {
            brush->SetColor(color(pal.accent, 0.55f * alpha));
            rt->FillRectangle(D2D1::RectF(body.x + w / 2, body.y, body.right(), body.y + h * seamY), brush.get());
        }
        rt->PopLayer();
    }
    brush->SetColor(color(pal.mouseEdge.rgb, pal.mouseEdge.alpha * alpha));
    rt->DrawGeometry(shape.get(), brush.get(), 1.5f);

    // Action button: a small vertical pill at the very front.
    const Rect pill{body.x + w / 2 - w * 0.0275f, body.y + h * 0.115f - h * 0.0425f, w * 0.055f, h * 0.085f};
    if (s.actionPressed) {
        brush->SetColor(color(pal.accent, 0.8f * alpha));
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(pill.x - 6, pill.y - 6, pill.right() + 6, pill.bottom() + 6), pill.w, pill.w), brush.get());
    }
    brush->SetColor(s.actionPressed ? color(pal.accent, alpha) : color(pal.isDark ? 0x1B1B1F : 0xCFD2D8, alpha));
    rt->FillRoundedRectangle(D2D1::RoundedRect(pill.d2d(), pill.w / 2, pill.w / 2), brush.get());
    brush->SetColor(pal.isDark ? D2D1::ColorF(1, 1, 1, 0.35f * alpha) : D2D1::ColorF(0, 0, 0, 0.25f * alpha));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(pill.d2d(), pill.w / 2, pill.w / 2), brush.get(), 1);

    // Wheel, below the action button, with notch lines whose phase follows the wheel.
    const Rect wheel{body.x + w / 2 - w * 0.05f, body.y + h * 0.285f - h * 0.065f, w * 0.10f, h * 0.13f};
    const bool pressed = (s.buttons & 0x04) != 0;
    brush->SetColor(pressed ? color(pal.accent, alpha) : color(pal.isDark ? 0x1B1B1F : 0xCFD2D8, alpha));
    rt->FillRoundedRectangle(D2D1::RoundedRect(wheel.d2d(), 6, 6), brush.get());
    const float step = wheel.h / 5;
    const float phase = static_cast<float>(((s.wheelTotal % 5) + 5) % 5) / 5 * step;
    brush->SetColor(pressed ? D2D1::ColorF(1, 1, 1, 0.7f * alpha) : color(pal.text3.rgb, pal.text3.alpha * alpha));
    for (int i = -1; i <= 5; ++i) {
        const float y = wheel.y + i * step + phase;
        if (y <= wheel.y + 2 || y >= wheel.bottom() - 2) continue;
        rt->DrawLine(D2D1::Point2F(wheel.x + 3, y), D2D1::Point2F(wheel.right() - 3, y), brush.get(), 1.2f);
    }
    brush->SetColor(color(pal.mouseEdge.rgb, pal.mouseEdge.alpha * alpha));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(wheel.d2d(), 6, 6), brush.get(), 1);
}

}  // namespace ixpat::ui
