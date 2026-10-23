#include "ui/im.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ui/glyphs.h"

namespace ixpat::ui {
namespace {

struct FormatKey {
    TextStyle style;
    bool operator<(const FormatKey& o) const { return static_cast<int>(style) < static_cast<int>(o.style); }
};
std::map<TextStyle, Com<IDWriteTextFormat>> gFormats;

struct FontSpec {
    const wchar_t* family;
    float size;
    DWRITE_FONT_WEIGHT weight;
};
FontSpec spec(TextStyle s) {
    switch (s) {
        case TextStyle::title: return {L"Segoe UI", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::heading: return {L"Segoe UI", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::body: return {L"Segoe UI", 12, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::bodyMedium: return {L"Segoe UI", 13, DWRITE_FONT_WEIGHT_MEDIUM};
        case TextStyle::detail: return {L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::footnote: return {L"Segoe UI", 10, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::footnoteSemibold: return {L"Segoe UI", 10, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::mono: return {L"Cascadia Mono", 11, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::monoSmall: return {L"Cascadia Mono", 10, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::caption: return {L"Segoe UI", 10, DWRITE_FONT_WEIGHT_MEDIUM};
        case TextStyle::wordmark: return {L"Segoe UI", 18, DWRITE_FONT_WEIGHT_BOLD};
    }
    return {L"Segoe UI", 12, DWRITE_FONT_WEIGHT_NORMAL};
}

constexpr float kPt = 96.0f / 72.0f;  // points -> DIPs
constexpr float kPopupRowH = 26, kPopupPad = 4;

std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}  // namespace

Frame::Frame(ID2D1RenderTarget* rt, const Palette& pal, const UiInput& in, UiState& st, float dpiScale, float winW, float winH)
    : rt_(rt), pal_(pal), in_(in), st_(st), scale_(dpiScale), winW_(winW), winH_(winH) {
    st_.anyDirty = false;
    st_.consumed = false;
    st_.wheelPending = false;
    // A click while a popup is up belongs to the popup: a row selects (applied by that dropdown on the rerun pass),
    // anywhere else just closes it. Either way nothing underneath sees the click.
    if (!rt_ && in.kind == UiInput::Kind::down && !st_.openDropdown.empty()) {
        for (const auto& p : st_.popups) {
            const Rect box = popupBox(p);
            if (!box.contains(in.x, in.y)) continue;
            const int i = static_cast<int>((in.y - box.y - kPopupPad) / kPopupRowH);
            if (i >= 0 && i < static_cast<int>(p.options.size())) {
                st_.pendingPopupId = p.id;
                st_.pendingPopupIndex = i;
                st_.rerun = true;
            }
        }
        st_.openDropdown.clear();
        st_.consumed = true;
    }
    st_.popups.clear();
    if (in.kind == UiInput::Kind::move || in.kind == UiInput::Kind::down || in.kind == UiInput::Kind::up || in.kind == UiInput::Kind::wheel) {
        st_.mouseX = in.x;
        st_.mouseY = in.y;
    } else if (in.kind == UiInput::Kind::leave) {
        st_.mouseX = st_.mouseY = -1;
    }
    if (rt_) rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush_.put());
    if (in.kind == UiInput::Kind::key && in.key == VK_ESCAPE && !st_.openDropdown.empty()) {
        st_.openDropdown.clear();
        st_.consumed = true;
    }
}

Frame::~Frame() = default;

// MARK: text

IDWriteTextFormat* Frame::format(TextStyle style) {
    auto it = gFormats.find(style);
    if (it != gFormats.end() && it->second) return it->second.get();
    const FontSpec f = spec(style);
    Com<IDWriteTextFormat> fmt;
    Gfx::dwrite()->CreateTextFormat(f.family, nullptr, f.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.size * kPt, L"en-us", fmt.put());
    if (!fmt && (style == TextStyle::mono || style == TextStyle::monoSmall)) {
        Gfx::dwrite()->CreateTextFormat(L"Consolas", nullptr, f.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.size * kPt, L"en-us", fmt.put());
    }
    IDWriteTextFormat* raw = fmt.get();
    gFormats[style] = std::move(fmt);
    return raw;
}

std::uint32_t Frame::textColor(TextStyle style, float& alpha) const {
    alpha = 1;
    switch (style) {
        case TextStyle::detail:
        case TextStyle::caption:
        case TextStyle::mono:
            alpha = pal_.text2.alpha;
            return pal_.text2.rgb;
        case TextStyle::footnote:
        case TextStyle::footnoteSemibold:
        case TextStyle::monoSmall:
            alpha = pal_.text3.alpha;
            return pal_.text3.rgb;
        default:
            return pal_.text;
    }
}

ID2D1SolidColorBrush* Frame::brush(std::uint32_t rgb, float alpha) {
    if (!brush_) return nullptr;
    brush_->SetColor(color(rgb, alpha));
    return brush_.get();
}

void Frame::drawText(std::wstring_view text, const Rect& r, TextStyle style, std::uint32_t rgb, float alpha, DWRITE_TEXT_ALIGNMENT align,
                     DWRITE_PARAGRAPH_ALIGNMENT valign, bool wrap) {
    if (!rt_ || text.empty()) return;
    IDWriteTextFormat* f = format(style);
    if (!f) return;
    Com<IDWriteTextLayout> layout;
    Gfx::dwrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), f, std::max(1.0f, r.w), std::max(1.0f, r.h), layout.put());
    if (!layout) return;
    layout->SetTextAlignment(align);
    layout->SetParagraphAlignment(valign);
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        Com<IDWriteInlineObject> ellipsis;
        Gfx::dwrite()->CreateEllipsisTrimmingSign(layout.get(), ellipsis.put());
        layout->SetTrimming(&trim, ellipsis.get());
    }
    rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout.get(), brush(rgb, alpha), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Frame::measureHeight(std::wstring_view text, TextStyle style, float width) {
    IDWriteTextFormat* f = format(style);
    if (!f || text.empty()) return 0;
    Com<IDWriteTextLayout> layout;
    Gfx::dwrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), f, std::max(1.0f, width), 10000.0f, layout.put());
    if (!layout) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return std::ceil(m.height);
}

float Frame::textWidth(std::wstring_view text, TextStyle style) {
    IDWriteTextFormat* f = format(style);
    if (!f || text.empty()) return 0;
    Com<IDWriteTextLayout> layout;
    Gfx::dwrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), f, 10000.0f, 1000.0f, layout.put());
    if (!layout) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return std::ceil(m.widthIncludingTrailingWhitespace);
}

// MARK: layout

void Frame::begin(Rect area) {
    area_ = area;
    cx_ = area.x;
    cy_ = area.y;
    inRow_ = false;
}

Rect Frame::take(float w, float h) {
    Rect r;
    if (inRow_) {
        r = {cx_, rowY_, w, h};
        cx_ += w;
        rowMaxH_ = std::max(rowMaxH_, h);
    } else {
        r = {area_.x, cy_, w <= 0 ? area_.w : w, h};
        cy_ += h;
    }
    last_ = r;
    return r;
}

void Frame::beginRow(float height) {
    inRow_ = true;
    rowY_ = cy_;
    rowH_ = height;
    rowMaxH_ = height;
    cx_ = area_.x;
}

void Frame::endRow() {
    inRow_ = false;
    cy_ = rowY_ + std::max(rowH_, rowMaxH_);
    cx_ = area_.x;
}

void Frame::space(float h) {
    if (inRow_) cx_ += h;
    else cy_ += h;
}

void Frame::gap(float w) { cx_ += w; }

void Frame::beginScroll(Rect viewport) {
    scrollViewport_ = viewport;
    inScroll_ = true;
    st_.wheelPending = !rt_ && in_.kind == UiInput::Kind::wheel && viewport.contains(in_.x, in_.y) && st_.openDropdown.empty();
    st_.viewHeight = viewport.h;
    if (rt_) rt_->PushAxisAlignedClip(viewport.d2d(), D2D1_ANTIALIAS_MODE_ALIASED);
    stack_.push_back({area_, cx_, cy_, inRow_, rowY_, rowH_, rowMaxH_});
    area_ = {viewport.x, viewport.y - st_.scrollY, viewport.w, 100000.0f};
    cx_ = area_.x;
    cy_ = area_.y;
    scrollStartY_ = cy_;
    inRow_ = false;
}

void Frame::endScroll() {
    st_.contentHeight = cy_ - scrollStartY_;
    if (st_.wheelPending && !st_.consumed) {  // no slider under the pointer took the notch
        st_.scrollY = std::clamp(st_.scrollY - in_.wheel * 0.5f, 0.0f, std::max(0.0f, st_.contentHeight - scrollViewport_.h));
    }
    st_.wheelPending = false;
    if (rt_) rt_->PopAxisAlignedClip();
    // Scroll bar
    if (rt_ && st_.contentHeight > scrollViewport_.h) {
        const float trackH = scrollViewport_.h - 8, frac = scrollViewport_.h / st_.contentHeight;
        const float thumbH = std::max(24.0f, trackH * frac);
        const float thumbY = scrollViewport_.y + 4 + (trackH - thumbH) * (st_.scrollY / (st_.contentHeight - scrollViewport_.h));
        fillRound({scrollViewport_.right() - 8, thumbY, 4, thumbH}, 2, pal_.text, 0.22f);
    }
    const Saved s = stack_.back();
    stack_.pop_back();
    area_ = s.area;
    cx_ = s.cx;
    cy_ = s.cy;
    inRow_ = s.inRow;
    rowY_ = s.rowY;
    rowH_ = s.rowH;
    rowMaxH_ = s.rowMaxH;
    inScroll_ = false;
    st_.scrollY = std::clamp(st_.scrollY, 0.0f, std::max(0.0f, st_.contentHeight - scrollViewport_.h));
}

void Frame::beginCard(float padding, bool accentTint) {
    stack_.push_back({area_, cx_, cy_, inRow_, rowY_, rowH_, rowMaxH_});
    const Rect outer{area_.x, cy_, area_.w, 0};
    cards_.push_back(outer);
    // Background is drawn at endCard (height known then) via a second pass trick: draw now with a large
    // height clipped later is wrong, so we draw behind using the previous frame's height isn't needed:
    // we record the card rect and the content; painting order would put content under it. Instead the
    // card is drawn here with its height from the layout of this same frame, which we get by laying out
    // twice (cheap). Simplest honest approach: cards store their rect and we draw the backgrounds first
    // in a pre-pass. To keep one pass, we draw the card after measuring content height in endCard by
    // using a layer: push a layer here, pop at endCard, then fill the card rect behind using
    // DrawingStateBlock ordering is unavailable. Pragmatic: draw the card background now with the height
    // from the last frame's measurement (stored per card index) and correct on the next paint.
    static std::vector<float> lastHeights;
    const std::size_t i = cards_.size() - 1;
    if (lastHeights.size() <= i) lastHeights.resize(i + 1, 200);
    cardHeights_ = &lastHeights;
    if (rt_) {
        const Rect bg{outer.x, outer.y, outer.w, lastHeights[i]};
        fillRound(bg, 18, pal_.panel, pal_.isDark ? 0.55f : 0.72f);
        if (accentTint) fillRound(bg, 18, pal_.accent, pal_.isDark ? 0.10f : 0.07f);
        // top-lit gloss + hairline rim
        if (brush_) {
            Com<ID2D1GradientStopCollection> stops;
            D2D1_GRADIENT_STOP gs[2] = {{0, D2D1::ColorF(1, 1, 1, pal_.isDark ? 0.10f : 0.60f)}, {1, D2D1::ColorF(1, 1, 1, pal_.isDark ? 0.02f : 0.18f)}};
            rt_->CreateGradientStopCollection(gs, 2, stops.put());
            Com<ID2D1LinearGradientBrush> lin;
            if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(bg.x, bg.y), D2D1::Point2F(bg.x, bg.bottom())), stops.get(), lin.put());
            if (lin) rt_->FillRoundedRectangle(D2D1::RoundedRect(bg.d2d(), 18, 18), lin.get());
        }
        strokeRound(bg, 18, 0xFFFFFF, pal_.isDark ? 0.18f : 0.6f, 1);
    }
    area_ = {outer.x + padding, cy_ + padding, outer.w - 2 * padding, 0};
    cx_ = area_.x;
    cy_ = area_.y;
    inRow_ = false;
    cardPadding_.push_back(padding);
}

void Frame::endCard() {
    const float padding = cardPadding_.back();
    cardPadding_.pop_back();
    const Rect outer = cards_.back();
    cards_.pop_back();
    const float height = cy_ - outer.y + padding;
    if (cardHeights_ && cardHeights_->size() > cards_.size()) (*cardHeights_)[cards_.size()] = height;
    const Saved s = stack_.back();
    stack_.pop_back();
    area_ = s.area;
    inRow_ = s.inRow;
    rowY_ = s.rowY;
    rowH_ = s.rowH;
    rowMaxH_ = s.rowMaxH;
    cx_ = area_.x;
    cy_ = outer.y + height;
}

void Frame::backdrop() {
    if (!rt_) return;
    Com<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[3] = {{0, color(pal_.stageTop)}, {0.5f, color(pal_.bg)}, {1, color(pal_.stageBottom)}};
    rt_->CreateGradientStopCollection(gs, 3, stops.put());
    Com<ID2D1LinearGradientBrush> lin;
    if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, winH_)), stops.get(), lin.put());
    if (lin) rt_->FillRectangle(D2D1::RectF(0, 0, winW_, winH_), lin.get());
    // Soft colour glows (radial gradients stand in for the Mac's blurred circles).
    auto glow = [&](float cx, float cy, float r, std::uint32_t rgb, float a) {
        Com<ID2D1GradientStopCollection> st2;
        D2D1_GRADIENT_STOP g2[2] = {{0, color(rgb, a)}, {1, color(rgb, 0)}};
        rt_->CreateGradientStopCollection(g2, 2, st2.put());
        Com<ID2D1RadialGradientBrush> rad;
        if (st2) rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), r, r), st2.get(), rad.put());
        if (rad) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), rad.get());
    };
    glow(winW_ * 0.15f, winH_ * 0.1f, 300, pal_.accent, pal_.isDark ? 0.30f : 0.26f);
    glow(winW_ * 0.9f, winH_ * 0.95f, 280, 0xBF5AF2, pal_.isDark ? 0.20f : 0.16f);
    glow(winW_ * 0.8f, winH_ * 0.15f, 200, 0x40C8E0, pal_.isDark ? 0.12f : 0.10f);
}

// MARK: primitives

void Frame::fillRound(const Rect& r, float radius, std::uint32_t rgb, float alpha) {
    if (!rt_) return;
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r.d2d(), radius, radius), brush(rgb, alpha));
}

void Frame::strokeRound(const Rect& r, float radius, std::uint32_t rgb, float alpha, float width) {
    if (!rt_) return;
    const Rect inset{r.x + width / 2, r.y + width / 2, r.w - width, r.h - width};
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(inset.d2d(), radius, radius), brush(rgb, alpha), width);
}

void Frame::dot(float cx, float cy, float r, std::uint32_t rgb, float alpha, bool glow) {
    if (!rt_) return;
    if (glow) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 2.2f, r * 2.2f), brush(rgb, alpha * 0.25f));
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush(rgb, alpha));
}

void Frame::glyph(wchar_t g, const Rect& r, std::uint32_t rgb, float alpha, float size) {
    if (!rt_ || !g) return;
    static std::map<int, Com<IDWriteTextFormat>> cache;
    const int key = static_cast<int>(size * 4);
    auto& fmt = cache[key];
    if (!fmt) {
        Gfx::dwrite()->CreateTextFormat(iconFontFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", fmt.put());
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmt) rt_->DrawTextW(&g, 1, fmt.get(), r.d2d(), brush(rgb, alpha));
}

void Frame::label(std::wstring_view text, TextStyle style, float width, std::uint32_t rgbOverride, float alphaOverride) {
    const float w = width > 0 ? width : (inRow_ ? textWidth(text, style) + 2 : area_.w);
    const float h = measureHeight(text, style, w);
    const Rect r = take(w, h);
    float alpha;
    std::uint32_t rgb = textColor(style, alpha);
    if (rgbOverride) rgb = rgbOverride;
    if (alphaOverride >= 0) alpha = alphaOverride;
    drawText(text, r, style, rgb, alpha);
}

void Frame::sectionTitle(std::wstring_view text) {
    beginRow(20);
    const Rect tick = take(3, 20);
    if (rt_) fillRound({tick.x, tick.y + 3, 3, 14}, 1.5f, pal_.accent, 1);
    gap(8);
    const Rect r = take(textWidth(text, TextStyle::title) + 4, 20);
    drawText(text, r, TextStyle::title, pal_.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    endRow();
}

void Frame::divider(float alpha) {
    const Rect r = take(0, 9);
    if (rt_) rt_->FillRectangle(D2D1::RectF(r.x, r.y + 4, r.right(), r.y + 5), brush(pal_.hair.rgb, pal_.hair.alpha * alpha));
}

// MARK: input helpers

bool Frame::hovered(const Rect& r) const {
    if (st_.consumed || st_.mouseX < 0) return false;
    if (inScroll_ && !scrollViewport_.contains(st_.mouseX, st_.mouseY)) return false;
    return r.contains(st_.mouseX, st_.mouseY);
}

bool Frame::clicked(const std::string& id, const Rect& r, bool enabled) {
    if (rt_ || st_.consumed || !enabled) return false;
    const bool over = hovered(r);
    if (in_.kind == UiInput::Kind::down && over && !in_.rightButton) {
        st_.active = id;
        if (st_.focus != id) st_.focus.clear();
        if (!st_.openDropdown.empty() && st_.openDropdown != id) st_.openDropdown.clear();
        return false;
    }
    if (in_.kind == UiInput::Kind::up && st_.active == id) {
        st_.active.clear();
        return over;
    }
    return false;
}

// MARK: widgets

bool Frame::toggle(const std::string& id, bool& value, std::wstring_view title, std::wstring_view detail, bool enabled) {
    const float sw = 38, sh = 22;
    const float textW = title.empty() ? 0 : (inRow_ ? textWidth(title, TextStyle::bodyMedium) + 10 : area_.w - sw - 12);
    float h = sh;
    float detailH = 0;
    if (!detail.empty() && !inRow_) {
        detailH = measureHeight(detail, TextStyle::detail, area_.w - sw - 12);
        h = std::max(h, 18 + detailH);
    }
    const Rect r = take(inRow_ ? sw + (title.empty() ? 0 : textW) : 0, h + 6);
    Rect sw_r{r.x, r.y + 3, sw, sh};
    Rect text_r{r.x + sw + 12, r.y + 3, r.w - sw - 12, 18};
    if (inRow_ && !title.empty()) {
        // title on the right of the switch in rows
        text_r = {r.x + sw + 10, r.y + 3, textW, sh};
    }
    bool changed = false;
    if (clicked(id, inRow_ ? r : Rect{r.x, r.y, r.w, r.h}, enabled)) {
        value = !value;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const bool hot = enabled && hovered(r);
        const float a = enabled ? 1.0f : 0.5f;
        fillRound(sw_r, sh / 2, value ? pal_.accent : pal_.track.rgb, value ? a : pal_.track.alpha * a * 1.6f);
        if (hot && !value) strokeRound(sw_r, sh / 2, pal_.text, 0.15f);
        const float kx = value ? sw_r.right() - sh / 2 : sw_r.x + sh / 2;
        dot(kx, sw_r.y + sh / 2, sh / 2 - 3, 0xFFFFFF, a);
        if (!title.empty()) drawText(title, text_r, TextStyle::bodyMedium, pal_.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        if (!detail.empty() && !inRow_) drawText(detail, {text_r.x, text_r.y + 18, text_r.w, detailH + 4}, TextStyle::detail, pal_.text2.rgb, pal_.text2.alpha * a);
    }
    return changed;
}

bool Frame::checkbox(const std::string& id, bool& value, std::wstring_view title) {
    const float box = 16;
    const float textW = textWidth(title, TextStyle::detail) + 8;
    const Rect r = take(box + 8 + textW, 22);
    bool changed = false;
    if (clicked(id, r)) {
        value = !value;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const Rect b{r.x, r.y + 3, box, box};
        fillRound(b, 4, value ? pal_.accent : pal_.field.rgb, value ? 1 : pal_.field.alpha);
        if (value) glyph(0xE73E, b, 0xFFFFFF, 1, 11);
        drawText(title, {r.x + box + 8, r.y, textW, 22}, TextStyle::detail, pal_.text2.rgb, pal_.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    return changed;
}

bool Frame::segmented(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled) {
    const float h = 24;
    const Rect r = take(width, h + 4);
    const Rect box{r.x, r.y + 2, width, h};
    const int n = static_cast<int>(options.size());
    if (n == 0) return false;
    const float segW = width / n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const Rect seg{box.x + segW * i, box.y, segW, h};
        if (clicked(id + "/" + std::to_string(i), seg, enabled) && index != i) {
            index = i;
            changed = true;
            st_.anyDirty = true;
        }
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.5f;
        fillRound(box, 7, pal_.field.rgb, pal_.field.alpha * a);
        if (index >= 0 && index < n) {
            const Rect sel{box.x + segW * index + 2, box.y + 2, segW - 4, h - 4};
            fillRound(sel, 6, pal_.isDark ? 0x636366 : 0xFFFFFF, a);
            strokeRound(sel, 6, 0x000000, 0.12f);
        }
        for (int i = 0; i < n; ++i) {
            const Rect seg{box.x + segW * i, box.y, segW, h};
            const bool on = i == index;
            drawText(options[static_cast<std::size_t>(i)], seg, on ? TextStyle::caption : TextStyle::footnote, pal_.text, on ? a : 0.75f * a, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    return changed;
}

bool Frame::slider(const std::string& id, double& value, double lo, double hi, double step, float width, std::wstring_view minLabel, std::wstring_view maxLabel, bool enabled) {
    const float h = 24;
    const float lw = minLabel.empty() ? 0 : textWidth(minLabel, TextStyle::footnote) + 6;
    const float rw = maxLabel.empty() ? 0 : textWidth(maxLabel, TextStyle::footnote) + 6;
    const Rect r = take(width, h + 2);
    const Rect track{r.x + lw, r.y + h / 2 - 2, width - lw - rw, 4};
    const Rect hit{r.x + lw - 8, r.y, width - lw - rw + 16, h};
    bool changed = false;
    if (!rt_ && enabled && !st_.consumed) {
        const bool over = hovered(hit);
        if (in_.kind == UiInput::Kind::down && over && !in_.rightButton) {
            st_.active = id;
            st_.focus.clear();
            st_.openDropdown.clear();
        }
        if (st_.active == id && (in_.kind == UiInput::Kind::down || in_.kind == UiInput::Kind::move)) {
            double t = (in_.x - track.x) / std::max(1.0f, track.w);
            t = std::clamp(t, 0.0, 1.0);
            double v = lo + t * (hi - lo);
            if (step > 0) v = lo + std::round((v - lo) / step) * step;
            v = std::clamp(v, lo, hi);
            if (v != value) {
                value = v;
                changed = true;
                st_.anyDirty = true;
            }
        }
        if (in_.kind == UiInput::Kind::up && st_.active == id) st_.active.clear();
        if (in_.kind == UiInput::Kind::wheel && over && st_.openDropdown.empty() && !st_.consumed) {
            const double s = step > 0 ? step : (hi - lo) / 20;
            const double v = std::clamp(value + (in_.wheel > 0 ? s : -s), lo, hi);
            if (v != value) {
                value = v;
                changed = true;
                st_.anyDirty = true;
                st_.consumed = true;
            }
        }
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.5f;
        const double t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0, 1.0) : 0;
        fillRound(track, 2, pal_.track.rgb, pal_.track.alpha * a);
        fillRound({track.x, track.y, static_cast<float>(track.w * t), 4}, 2, pal_.accent, a);
        const float kx = track.x + static_cast<float>(track.w * t);
        dot(kx, track.y + 2, 8, 0xFFFFFF, a);
        if (rt_) rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(kx, track.y + 2), 8, 8), brush(0x000000, 0.18f * a), 1);
        if (!minLabel.empty()) drawText(minLabel, {r.x, r.y, lw, h}, TextStyle::footnote, pal_.text3.rgb, pal_.text3.alpha * a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        if (!maxLabel.empty()) drawText(maxLabel, {r.right() - rw, r.y, rw, h}, TextStyle::footnote, pal_.text3.rgb, pal_.text3.alpha * a, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    return changed;
}

bool Frame::stepper(const std::string& id, int& value, int lo, int hi, int step, std::wstring_view text, float width, bool enabled) {
    const float h = 24, bw = 22;
    const Rect r = take(width, h + 2);
    const Rect minus{r.right() - 2 * bw - 1, r.y + 1, bw, h}, plus{r.right() - bw, r.y + 1, bw, h};
    bool changed = false;
    if (clicked(id + "/-", minus, enabled && value > lo)) {
        value = std::max(lo, value - step);
        changed = true;
        st_.anyDirty = true;
    }
    if (clicked(id + "/+", plus, enabled && value < hi)) {
        value = std::min(hi, value + step);
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.5f;
        drawText(text, {r.x, r.y, width - 2 * bw - 8, h + 2}, TextStyle::detail, pal_.text2.rgb, pal_.text2.alpha * a, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        fillRound({minus.x, minus.y, 2 * bw + 1, h}, 6, pal_.field.rgb, pal_.field.alpha * a);
        rt_->FillRectangle(D2D1::RectF(plus.x - 0.5f, minus.y + 5, plus.x + 0.5f, minus.bottom() - 5), brush(pal_.hair.rgb, pal_.hair.alpha));
        glyph(0xE738, minus, pal_.text, (value > lo ? 1.0f : 0.35f) * a, 10);
        glyph(0xE710, plus, pal_.text, (value < hi ? 1.0f : 0.35f) * a, 10);
    }
    return changed;
}

bool Frame::button(const std::string& id, std::wstring_view text, bool prominent, bool enabled, float width) {
    const float h = 26;
    const float w = width > 0 ? width : textWidth(text, TextStyle::bodyMedium) + 26;
    const Rect r = take(w, h + 2);
    const Rect b{r.x, r.y + 1, w, h};
    const bool hit = clicked(id, b, enabled);
    if (rt_) {
        const bool hot = enabled && hovered(b);
        const bool down = st_.active == id;
        const float a = enabled ? 1.0f : 0.45f;
        if (prominent) {
            fillRound(b, 8, pal_.accent, (down ? 0.75f : hot ? 0.9f : 1.0f) * a);
            strokeRound(b, 8, 0xFFFFFF, 0.25f * a);
            drawText(text, b, TextStyle::bodyMedium, 0xFFFFFF, a, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        } else {
            fillRound(b, 8, pal_.isDark ? 0xFFFFFF : 0x000000, (down ? 0.18f : hot ? 0.13f : 0.08f) * a);
            strokeRound(b, 8, pal_.hair.rgb, pal_.hair.alpha * 1.5f * a);
            drawText(text, b, TextStyle::bodyMedium, pal_.text, a, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    return hit;
}

bool Frame::iconButton(const std::string& id, wchar_t g, bool enabled, std::wstring_view) {
    const float s = 26;
    const Rect r = take(s + 2, s + 2);
    const Rect b{r.x, r.y + 1, s, s};
    const bool hit = clicked(id, b, enabled);
    if (rt_) {
        const bool hot = enabled && hovered(b);
        const float a = enabled ? 1.0f : 0.4f;
        fillRound(b, 8, pal_.isDark ? 0xFFFFFF : 0x000000, (hot ? 0.14f : 0.08f) * a);
        strokeRound(b, 8, pal_.hair.rgb, pal_.hair.alpha * 1.5f * a);
        glyph(g, b, pal_.text, a, 12);
    }
    return hit;
}

bool Frame::dropdown(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled) {
    const float h = 26;
    const Rect r = take(width, h + 2);
    const Rect b{r.x, r.y + 1, width, h};
    bool changed = false;
    if (!rt_ && st_.pendingPopupId == id) {  // the popup row clicked on the previous pass
        st_.pendingPopupId.clear();
        if (index != st_.pendingPopupIndex && st_.pendingPopupIndex >= 0 && st_.pendingPopupIndex < static_cast<int>(options.size())) {
            index = st_.pendingPopupIndex;
            changed = true;
            st_.anyDirty = true;
        }
    }
    if (!rt_ && enabled && !st_.consumed) {
        if (in_.kind == UiInput::Kind::down && hovered(b) && !in_.rightButton) {
            st_.openDropdown = id;
            st_.focus.clear();
            st_.consumed = true;
        }
    }
    if (st_.openDropdown == id) {
        UiState::Popup p;
        p.id = id;
        p.anchor = b;
        p.options.assign(options.begin(), options.end());
        p.current = index;
        p.width = width;
        st_.popups.push_back(std::move(p));
    }
    if (rt_) {
        const bool hot = enabled && hovered(b);
        const float a = enabled ? 1.0f : 0.45f;
        fillRound(b, 8, pal_.isDark ? 0xFFFFFF : 0x000000, (hot ? 0.13f : 0.08f) * a);
        strokeRound(b, 8, pal_.hair.rgb, pal_.hair.alpha * 1.5f * a);
        const std::wstring text = index >= 0 && index < static_cast<int>(options.size()) ? options[static_cast<std::size_t>(index)] : L"";
        drawText(text, {b.x + 10, b.y, b.w - 32, b.h}, TextStyle::body, pal_.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        glyph(0xE70D, {b.right() - 24, b.y, 20, b.h}, pal_.text2.rgb, pal_.text2.alpha * a, 9);
    }
    return changed;
}

Rect Frame::popupBox(const UiState::Popup& p) const {
    const float h = kPopupPad * 2 + kPopupRowH * static_cast<float>(p.options.size());
    float y = p.anchor.bottom() + 2;
    if (y + h > winH_ - 8) y = std::max(8.0f, p.anchor.y - 2 - h);  // flip up near the bottom edge
    const float w = std::max(p.width, 120.0f);
    const float x = std::min(p.anchor.x, winW_ - w - 8);
    return {x, y, w, h};
}

bool Frame::textField(const std::string& id, std::string& value, float width, std::wstring_view placeholder, bool mono, bool enabled) {
    const float h = 26;
    const Rect r = take(width, h + 2);
    const Rect b{r.x, r.y + 1, width, h};
    bool changed = false;
    if (!rt_ && enabled && !st_.consumed && in_.kind == UiInput::Kind::down && hovered(b) && !in_.rightButton) {
        st_.focus = id;
        st_.openDropdown.clear();
        st_.consumed = true;
        st_.editTarget = &value;
        st_.editId = id;
        if (st_.beginEdit) st_.beginEdit(nullptr, b, id);
    }
    if (st_.editId == id && st_.editCommitted) {
        st_.editCommitted = false;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.45f;
        fillRound(b, 8, pal_.field.rgb, pal_.field.alpha * a);
        strokeRound(b, 8, st_.focus == id ? pal_.accent : pal_.hair.rgb, st_.focus == id ? 0.9f : pal_.hair.alpha * 1.5f * a);
        if (st_.editId != id || !st_.edit) {
            const std::wstring text = wide(value);
            if (text.empty()) drawText(placeholder, {b.x + 8, b.y, b.w - 16, b.h}, mono ? TextStyle::mono : TextStyle::body, pal_.text3.rgb, pal_.text3.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            else drawText(text, {b.x + 8, b.y, b.w - 16, b.h}, mono ? TextStyle::mono : TextStyle::body, pal_.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    if (st_.editId == id) st_.editRect = b;  // the owner keeps the native control aligned
    return changed;
}

bool Frame::pillTabs(const std::string& id, int& index, std::span<const std::wstring> titles) {
    const float h = 30, padX = 14;
    float total = 8;
    std::vector<float> widths;
    for (const auto& t : titles) {
        const float w = textWidth(t, TextStyle::caption) + 2 * padX;
        widths.push_back(w);
        total += w + 2;
    }
    const Rect r = take(total, h + 4);
    const Rect bar{r.x, r.y + 2, total, h};
    bool changed = false;
    float x = bar.x + 4;
    for (std::size_t i = 0; i < titles.size(); ++i) {
        const Rect pill{x, bar.y + 4, widths[i], h - 8};
        if (clicked(id + "/" + std::to_string(i), pill) && index != static_cast<int>(i)) {
            index = static_cast<int>(i);
            changed = true;
        }
        x += widths[i] + 2;
    }
    if (rt_) {
        fillRound(bar, h / 2, pal_.field.rgb, pal_.field.alpha);
        strokeRound(bar, h / 2, pal_.hair.rgb, pal_.hair.alpha);
        x = bar.x + 4;
        for (std::size_t i = 0; i < titles.size(); ++i) {
            const Rect pill{x, bar.y + 4, widths[i], h - 8};
            const bool on = index == static_cast<int>(i);
            if (on) {
                fillRound({pill.x, pill.y + 2, pill.w, pill.h}, pill.h / 2, pal_.accent, 0.35f);  // glow
                fillRound(pill, pill.h / 2, pal_.accent, 1);
                strokeRound(pill, pill.h / 2, 0xFFFFFF, 0.35f);
            }
            drawText(titles[i], pill, on ? TextStyle::caption : TextStyle::caption, on ? 0xFFFFFF : pal_.text2.rgb, on ? 1 : pal_.text2.alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            x += widths[i] + 2;
        }
    }
    return changed;
}

bool Frame::listRow(const std::string& id, bool selected, float height) {
    const Rect r = take(0, height);
    const bool hit = clicked(id, r);
    if (rt_) {
        if (selected) fillRound(r, 9, pal_.accent, 0.18f);
        else if (hovered(r)) fillRound(r, 9, pal_.rowHover.rgb, pal_.rowHover.alpha);
    }
    return hit;
}

// MARK: popups and finish

void Frame::drawPopups() {
    if (!rt_) return;  // input against popups is resolved at the start of the next pass (Frame::Frame)
    for (auto& p : st_.popups) {
        const float rowH = kPopupRowH, pad = kPopupPad;
        const Rect box = popupBox(p);
        const float w = box.w;
        rt_->FillRoundedRectangle(D2D1::RoundedRect({box.x + 2, box.y + 6, box.right() + 2, box.bottom() + 6}, 10, 10), brush(0x000000, 0.25f));
        fillRound(box, 10, pal_.isDark ? 0x2C2C2E : 0xFFFFFF, 1);
        strokeRound(box, 10, pal_.hair.rgb, pal_.hair.alpha * 2);
        for (std::size_t i = 0; i < p.options.size(); ++i) {
            const Rect row{box.x + pad, box.y + pad + rowH * static_cast<float>(i), w - 2 * pad, rowH};
            const bool on = p.current == static_cast<int>(i);
            const bool hot = row.contains(st_.mouseX, st_.mouseY);
            if (hot) fillRound(row, 6, pal_.accent, 0.85f);
            drawText(p.options[i], {row.x + 10, row.y, row.w - 20, row.h}, TextStyle::body, hot ? 0xFFFFFF : pal_.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            if (on) glyph(0xE73E, {row.right() - 24, row.y, 20, row.h}, hot ? 0xFFFFFF : pal_.accent, 1, 10);
        }
    }
}

void Frame::finish() {
    drawPopups();
    if (!rt_ && st_.anyDirty && st_.onChanged) st_.onChanged();
}

}  // namespace ixpat::ui
