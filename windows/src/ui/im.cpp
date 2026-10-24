#include "ui/im.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "ui/glyphs.h"

namespace ixpat::ui {
namespace {

std::map<TextStyle, Com<IDWriteTextFormat>> gFormats;

struct FontSpec {
    const wchar_t* family;
    float px;
    DWRITE_FONT_WEIGHT weight;
};
constexpr const wchar_t* kText = L"Segoe UI Variable Text";
constexpr const wchar_t* kDisplay = L"Segoe UI Variable Display";
FontSpec spec(TextStyle s) {
    switch (s) {
        case TextStyle::pageTitle: return {kDisplay, 28, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::subtitle: return {kDisplay, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::wordmark: return {kDisplay, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::heading: return {kText, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::title: return {kText, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::bodyMedium: return {kText, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::body: return {kText, 14, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::detail: return {kText, 12, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::caption: return {kText, 12, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::footnote: return {kText, 12, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::footnoteSemibold: return {kText, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case TextStyle::mono: return {L"Cascadia Mono", 12, DWRITE_FONT_WEIGHT_NORMAL};
        case TextStyle::monoSmall: return {L"Cascadia Mono", 12, DWRITE_FONT_WEIGHT_NORMAL};
    }
    return {kText, 14, DWRITE_FONT_WEIGHT_NORMAL};
}

constexpr float kControlH = 32;  // every Fluent control is 32 DIPs tall
constexpr float kRadius = 4;     // ControlCornerRadius
constexpr float kFlyoutRadius = 8;
constexpr float kPopupRowH = 36, kPopupPad = 4;

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
            if (!box.contains(in.x, in.y) || in.rightButton) continue;
            const Rect rows{box.x + kPopupPad, box.y + kPopupPad, box.w - 2 * kPopupPad, kPopupRowH * static_cast<float>(p.options.size())};
            if (!rows.contains(in.x, in.y)) continue;  // the padding selects nothing
            const int i = static_cast<int>(std::floor((in.y - rows.y) / kPopupRowH));
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
    if (!Gfx::dwrite()) return nullptr;
    const FontSpec f = spec(style);
    Com<IDWriteTextFormat> fmt;
    Gfx::dwrite()->CreateTextFormat(f.family, nullptr, f.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.px, L"en-us", fmt.put());
    if (!fmt && (style == TextStyle::mono || style == TextStyle::monoSmall)) {
        Gfx::dwrite()->CreateTextFormat(L"Consolas", nullptr, f.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.px, L"en-us", fmt.put());
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
    if (!f) return;  // no factory, no text (format() returns null without one)
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
    // Scroll bar: the thin Fluent rail, at the viewport's right edge.
    if (rt_ && st_.contentHeight > scrollViewport_.h) {
        const float trackH = scrollViewport_.h - 8, frac = scrollViewport_.h / st_.contentHeight;
        const float thumbH = std::max(24.0f, trackH * frac);
        const float thumbY = scrollViewport_.y + 4 + (trackH - thumbH) * (st_.scrollY / (st_.contentHeight - scrollViewport_.h));
        fillRound({scrollViewport_.right() - 7, thumbY, 3, thumbH}, 1.5f, pal_.text3.rgb, pal_.text3.alpha);
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

// MARK: surfaces

void Frame::layer(const Rect& r) {
    if (!rt_) return;
    // Extend past the window's right and bottom edges so only the top-left corner shows its radius.
    const Rect big{r.x, r.y, r.w + 16, r.h + 16};
    fillRound(big, kFlyoutRadius, pal_.layer.rgb, pal_.layer.alpha);
    strokeRound(big, kFlyoutRadius, pal_.cardStroke.rgb, pal_.cardStroke.alpha);
}

void Frame::beginCard(float padding, bool accentTint) {
    stack_.push_back({area_, cx_, cy_, inRow_, rowY_, rowH_, rowMaxH_});
    const Rect outer{area_.x, cy_, area_.w, 0};
    cards_.push_back(outer);
    // The card's height is only known once its content is laid out, so the background is drawn with the
    // height this card had on the previous pass; the owner runs a layout pass right before every paint.
    const std::size_t slot = cardSeq_++;
    if (st_.cardHeights.size() <= slot) st_.cardHeights.resize(slot + 1, 0);
    cardIndex_.push_back(slot);
    if (rt_ && st_.cardHeights[slot] > 0) {
        const Rect bg{outer.x, outer.y, outer.w, st_.cardHeights[slot]};
        fillRound(bg, kRadius, pal_.card.rgb, pal_.card.alpha);
        if (accentTint) fillRound(bg, kRadius, pal_.accent, 0.08f);
        strokeRound(bg, kRadius, pal_.cardStroke.rgb, pal_.cardStroke.alpha);
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
    const std::size_t slot = cardIndex_.back();
    cardIndex_.pop_back();
    const float height = cy_ - outer.y + padding;
    if (slot < st_.cardHeights.size()) st_.cardHeights[slot] = height;
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

void Frame::beginSections(float padding) {
    inSections_ = true;
    sectionPadding_ = padding;
    beginCard(padding);
}

void Frame::sectionBreak() {
    if (!inSections_) {
        divider();
        return;
    }
    endCard();
    space(4);
    beginCard(sectionPadding_);
}

void Frame::endSections() {
    if (!inSections_) return;
    endCard();
    inSections_ = false;
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
    if (!fmt && Gfx::dwrite()) {
        Gfx::dwrite()->CreateTextFormat(iconFontFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", fmt.put());
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (fmt) rt_->DrawTextW(&g, 1, fmt.get(), r.d2d(), brush(rgb, alpha));
}

void Frame::controlSurface(const Rect& b, bool hot, bool down, bool enabled) {
    if (!rt_) return;
    const Tint fill = !enabled ? Tint{pal_.controlFill.rgb, pal_.controlFill.alpha * 0.6f} : down ? pal_.controlFillPressed : hot ? pal_.controlFillHover : pal_.controlFill;
    fillRound(b, kRadius, fill.rgb, fill.alpha);
    strokeRound(b, kRadius, pal_.controlStroke.rgb, pal_.controlStroke.alpha);
    if (enabled && !down) {
        // The elevation border: one edge a shade apart, so the control sits on the surface instead of in it.
        const float y = pal_.edgeOnTop ? b.y + 0.5f : b.bottom() - 0.5f;
        rt_->DrawLine(D2D1::Point2F(b.x + kRadius, y), D2D1::Point2F(b.right() - kRadius, y), brush(pal_.controlEdge.rgb, pal_.controlEdge.alpha), 1);
    }
}

void Frame::selectionPill(const Rect& row) {
    if (!rt_) return;
    fillRound({row.x, row.y + row.h / 2 - 8, 3, 16}, 1.5f, pal_.accent, 1);
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

void Frame::divider(float alpha) {
    const Rect r = take(0, 17);
    if (rt_) rt_->FillRectangle(D2D1::RectF(r.x, r.y + 8, r.right(), r.y + 9), brush(pal_.hair.rgb, pal_.hair.alpha * alpha));
}

// MARK: input helpers

bool Frame::hovered(const Rect& r) const {
    if (st_.consumed || st_.mouseX < 0) return false;
    if (inScroll_ && !scrollViewport_.contains(st_.mouseX, st_.mouseY)) return false;
    return r.contains(st_.mouseX, st_.mouseY);
}

bool Frame::clicked(const std::string& id, const Rect& r, bool enabled) {
    if (rt_) return false;
    // A release or a cancel ends this widget's press whatever its state now (disabled since, consumed, elsewhere).
    if ((in_.kind == UiInput::Kind::up || in_.kind == UiInput::Kind::cancel) && st_.active == id) {
        st_.active.clear();
        return in_.kind == UiInput::Kind::up && enabled && !st_.consumed && !in_.rightButton && hovered(r);
    }
    if (st_.consumed || !enabled) return false;
    if (in_.kind == UiInput::Kind::down && hovered(r) && !in_.rightButton) {
        st_.active = id;
        if (st_.focus != id) st_.focus.clear();
        if (!st_.openDropdown.empty() && st_.openDropdown != id) st_.openDropdown.clear();
    }
    return false;
}

// MARK: widgets

bool Frame::toggle(const std::string& id, bool& value, std::wstring_view title, std::wstring_view detail, bool enabled) {
    const float sw = 40, sh = 20;
    const bool inline_ = inRow_ || title.empty();
    float textW = 0, detailH = 0, h = sh;
    if (!inline_) {
        // A settings row: the header and its description on the left, the switch at the right edge.
        textW = area_.w - sw - 16;
        detailH = detail.empty() ? 0 : measureHeight(detail, TextStyle::detail, textW);
        h = std::max(sh, 20 + (detailH > 0 ? detailH + 2 : 0));
    } else if (!title.empty()) {
        textW = textWidth(title, TextStyle::body) + 12;
    }
    const Rect r = take(inline_ ? sw + textW : 0, h + 12);
    const Rect sw_r = inline_ ? Rect{r.x, r.y + 6 + (h - sh) / 2, sw, sh} : Rect{r.right() - sw, r.y + 6, sw, sh};
    bool changed = false;
    if (clicked(id, r, enabled)) {
        value = !value;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const bool hot = enabled && hovered(r);
        const bool down = st_.active == id;
        const float a = enabled ? 1.0f : 0.4f;
        const float knob = (hot || down) ? 7.0f : 6.0f;
        if (value) {
            fillRound(sw_r, sh / 2, down ? pal_.accentPressed : hot ? pal_.accentHover : pal_.accent, a);
            dot(sw_r.right() - 10, sw_r.y + sh / 2, knob, pal_.textOnAccent, a);
        } else {
            fillRound(sw_r, sh / 2, pal_.controlAlt.rgb, pal_.controlAlt.alpha);
            strokeRound(sw_r, sh / 2, pal_.strongStroke.rgb, pal_.strongStroke.alpha * a);
            dot(sw_r.x + 10, sw_r.y + sh / 2, knob, pal_.text2.rgb, pal_.text2.alpha * a);
        }
        if (!title.empty()) {
            const Rect text_r = inline_ ? Rect{sw_r.right() + 12, sw_r.y - 6, textW, sh + 12} : Rect{r.x, r.y + 6, textW, 20};
            drawText(title, text_r, TextStyle::body, enabled ? pal_.text : pal_.textDisabled.rgb, enabled ? 1 : pal_.textDisabled.alpha,
                     DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            if (!inline_ && !detail.empty()) drawText(detail, {r.x, r.y + 26, textW, detailH + 4}, TextStyle::detail, pal_.text2.rgb, pal_.text2.alpha * a);
        }
    }
    return changed;
}

bool Frame::checkbox(const std::string& id, bool& value, std::wstring_view title) {
    const float box = 20;
    const float textW = textWidth(title, TextStyle::body) + 8;
    const Rect r = take(box + 8 + textW, kControlH);
    bool changed = false;
    if (clicked(id, r)) {
        value = !value;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const bool hot = hovered(r);
        const Rect b{r.x, r.y + (kControlH - box) / 2, box, box};
        if (value) {
            fillRound(b, kRadius, hot ? pal_.accentHover : pal_.accent, 1);
            glyph(0xE73E, b, pal_.textOnAccent, 1, 12);
        } else {
            fillRound(b, kRadius, pal_.controlAlt.rgb, pal_.controlAlt.alpha * (hot ? 1.6f : 1));
            strokeRound(b, kRadius, pal_.strongStroke.rgb, pal_.strongStroke.alpha);
        }
        drawText(title, {r.x + box + 8, r.y, textW, kControlH}, TextStyle::body, pal_.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    return changed;
}

bool Frame::segmented(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled) {
    // A selector bar: plain items, a hover wash, and the accent pill under the chosen one.
    const Rect r = take(width, kControlH);
    const int n = static_cast<int>(options.size());
    if (n == 0) return false;
    const float segW = width / n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const Rect seg{r.x + segW * i, r.y, segW, kControlH};
        if (clicked(id + "/" + std::to_string(i), seg, enabled) && index != i) {
            index = i;
            changed = true;
            st_.anyDirty = true;
        }
    }
    if (rt_) {
        for (int i = 0; i < n; ++i) {
            const Rect seg{r.x + segW * i, r.y, segW, kControlH};
            const bool on = i == index;
            const bool hot = enabled && hovered(seg);
            if (hot) fillRound({seg.x + 1, seg.y, seg.w - 2, seg.h}, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha);
            const std::uint32_t rgb = !enabled ? pal_.textDisabled.rgb : on ? pal_.text : pal_.text2.rgb;
            const float a = !enabled ? pal_.textDisabled.alpha : on ? 1 : pal_.text2.alpha;
            drawText(options[static_cast<std::size_t>(i)], {seg.x, seg.y, seg.w, seg.h - 2}, on ? TextStyle::bodyMedium : TextStyle::body, rgb, a,
                     DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            if (on) fillRound({seg.x + seg.w / 2 - 8, seg.bottom() - 4, 16, 3}, 1.5f, pal_.accent, enabled ? 1 : 0.4f);
        }
    }
    return changed;
}

bool Frame::slider(const std::string& id, double& value, double lo, double hi, double step, float width, std::wstring_view minLabel, std::wstring_view maxLabel, bool enabled) {
    const float h = kControlH;
    const float lw = minLabel.empty() ? 0 : textWidth(minLabel, TextStyle::detail) + 8;
    const float rw = maxLabel.empty() ? 0 : textWidth(maxLabel, TextStyle::detail) + 8;
    const Rect r = take(width, h);
    const Rect track{r.x + lw + 10, r.y + h / 2 - 2, width - lw - rw - 20, 4};
    const Rect hit{r.x + lw, r.y, width - lw - rw, h};
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
            // Whole notches only: the smooth-scroll stream delivers a notch as several small deltas.
            if (st_.wheelAccumId != id) {
                st_.wheelAccumId = id;
                st_.wheelAccum = 0;
            }
            st_.wheelAccum += in_.wheel;
            const int notches = static_cast<int>(st_.wheelAccum / 120);
            st_.wheelAccum -= static_cast<float>(notches) * 120.0f;
            st_.consumed = true;  // the slider took the event, whether or not it moved
            const double s = step > 0 ? step : (hi - lo) / 20;
            const double v = std::clamp(value + notches * s, lo, hi);
            if (v != value) {
                value = v;
                changed = true;
                st_.anyDirty = true;
            }
        }
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.4f;
        const bool hot = enabled && hovered(hit);
        const bool down = st_.active == id;
        const double t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0, 1.0) : 0;
        fillRound(track, 2, pal_.strongFill.rgb, pal_.strongFill.alpha * a);
        fillRound({track.x, track.y, static_cast<float>(track.w * t), 4}, 2, pal_.accent, a);
        // The thumb: a 20 px disc of the surface with the accent at its centre (bigger on hover, smaller pressed).
        const float kx = track.x + static_cast<float>(track.w * t), ky = track.y + 2;
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ky), 10, 10), brush(pal_.solidFill, a));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ky), 9.5f, 9.5f), brush(pal_.controlEdge.rgb, pal_.controlEdge.alpha * 2 * a), 1);
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ky), down ? 5.0f : hot ? 7.0f : 6.0f, down ? 5.0f : hot ? 7.0f : 6.0f), brush(pal_.accent, a));
        if (!minLabel.empty()) drawText(minLabel, {r.x, r.y, lw, h}, TextStyle::detail, pal_.text3.rgb, pal_.text3.alpha * a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        if (!maxLabel.empty()) drawText(maxLabel, {r.right() - rw, r.y, rw, h}, TextStyle::detail, pal_.text3.rgb, pal_.text3.alpha * a, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    return changed;
}

bool Frame::stepper(const std::string& id, int& value, int lo, int hi, int step, std::wstring_view text, float width, bool enabled) {
    // A number box: the value in a text-box surface, the spin buttons stacked inside its right end.
    const float bw = 28;
    const Rect r = take(width, kControlH);
    const Rect up{r.right() - bw - 2, r.y + 2, bw, kControlH / 2 - 2}, down{r.right() - bw - 2, r.y + kControlH / 2, bw, kControlH / 2 - 2};
    bool changed = false;
    if (clicked(id + "/+", up, enabled && value < hi)) {
        value = std::min(hi, value + step);
        changed = true;
        st_.anyDirty = true;
    }
    if (clicked(id + "/-", down, enabled && value > lo)) {
        value = std::max(lo, value - step);
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.4f;
        controlSurface(r, false, false, enabled);
        rt_->FillRectangle(D2D1::RectF(r.x + kRadius, r.bottom() - 1, r.right() - kRadius, r.bottom()), brush(pal_.strongStroke.rgb, pal_.strongStroke.alpha * a));
        drawText(text, {r.x + 12, r.y, width - bw - 20, kControlH}, TextStyle::body, pal_.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        for (const auto& [b, g, ok] : {std::tuple{up, wchar_t{0xE70E}, value < hi}, std::tuple{down, wchar_t{0xE70D}, value > lo}}) {
            if (enabled && ok && hovered(b)) fillRound(b, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha);
            glyph(g, b, pal_.text2.rgb, pal_.text2.alpha * (ok ? a : 0.4f * a), 8);
        }
    }
    return changed;
}

bool Frame::button(const std::string& id, std::wstring_view text, bool prominent, bool enabled, float width) {
    const float w = width > 0 ? width : std::max(64.0f, textWidth(text, TextStyle::body) + 24);
    const Rect b = take(w, kControlH);
    const bool hit = clicked(id, b, enabled);
    if (rt_) {
        const bool hot = enabled && hovered(b);
        const bool down = st_.active == id;
        if (prominent) {
            fillRound(b, kRadius, down ? pal_.accentPressed : hot ? pal_.accentHover : pal_.accent, enabled ? 1 : 0.4f);
            if (enabled && !down) strokeRound(b, kRadius, pal_.isDark ? 0xFFFFFF : 0x000000, pal_.isDark ? 0.08f : 0.12f);
            drawText(text, b, TextStyle::body, pal_.textOnAccent, enabled ? (down ? 0.8f : 1) : 0.6f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        } else {
            controlSurface(b, hot, down, enabled);
            drawText(text, b, TextStyle::body, enabled ? (down ? pal_.text2.rgb : pal_.text) : pal_.textDisabled.rgb,
                     enabled ? (down ? pal_.text2.alpha : 1) : pal_.textDisabled.alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    return hit;
}

bool Frame::iconButton(const std::string& id, wchar_t g, bool enabled, std::wstring_view) {
    const Rect b = take(kControlH, kControlH);
    const bool hit = clicked(id, b, enabled);
    if (rt_) {
        const bool hot = enabled && hovered(b);
        const bool down = st_.active == id;
        if (down) fillRound(b, kRadius, pal_.subtlePressed.rgb, pal_.subtlePressed.alpha);
        else if (hot) fillRound(b, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha);
        glyph(g, b, enabled ? pal_.text : pal_.textDisabled.rgb, enabled ? (down ? 0.7f : 1) : pal_.textDisabled.alpha, 16);
    }
    return hit;
}

bool Frame::dropdown(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled) {
    const Rect b = take(width, kControlH);
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
        const bool open = st_.openDropdown == id;
        controlSurface(b, hot, open, enabled);
        const std::wstring text = index >= 0 && index < static_cast<int>(options.size()) ? options[static_cast<std::size_t>(index)] : L"";
        drawText(text, {b.x + 12, b.y, b.w - 44, b.h}, TextStyle::body, enabled ? pal_.text : pal_.textDisabled.rgb, enabled ? 1 : pal_.textDisabled.alpha,
                 DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        glyph(0xE70D, {b.right() - 30, b.y, 20, b.h}, enabled ? pal_.text2.rgb : pal_.textDisabled.rgb, enabled ? pal_.text2.alpha : pal_.textDisabled.alpha, 10);
    }
    return changed;
}

Rect Frame::popupBox(const UiState::Popup& p) const {
    const float h = kPopupPad * 2 + kPopupRowH * static_cast<float>(p.options.size());
    float y = p.anchor.bottom() + 4;
    if (y + h > winH_ - 8) y = std::max(8.0f, p.anchor.y - 4 - h);  // flip up near the bottom edge
    const float w = std::max(p.width, 120.0f);
    const float x = std::min(p.anchor.x, winW_ - w - 8);
    return {x, y, w, h};
}

bool Frame::textField(const std::string& id, std::string& value, float width, std::wstring_view placeholder, bool mono, bool enabled) {
    const Rect b = take(width, kControlH);
    bool changed = false;
    if (!rt_ && enabled && !st_.consumed && in_.kind == UiInput::Kind::down && hovered(b) && !in_.rightButton) {
        st_.focus = id;
        st_.openDropdown.clear();
        st_.consumed = true;
        st_.editTarget = &value;
        st_.editId = id;
        st_.editMono = mono;
        if (st_.beginEdit) st_.beginEdit(nullptr, b, id);
    }
    if (st_.editId == id && st_.editCommitted) {
        st_.editCommitted = false;
        changed = true;
        st_.anyDirty = true;
    }
    if (rt_) {
        const float a = enabled ? 1.0f : 0.4f;
        const bool focused = st_.focus == id;
        fillRound(b, kRadius, pal_.controlFill.rgb, pal_.controlFill.alpha * a);
        strokeRound(b, kRadius, pal_.controlStroke.rgb, pal_.controlStroke.alpha);
        // The text box's own elevation line: a hair of the strong stroke along the bottom, the accent while focused.
        if (focused) rt_->FillRectangle(D2D1::RectF(b.x + kRadius, b.bottom() - 2, b.right() - kRadius, b.bottom()), brush(pal_.accent, 1));
        else rt_->FillRectangle(D2D1::RectF(b.x + kRadius, b.bottom() - 1, b.right() - kRadius, b.bottom()), brush(pal_.strongStroke.rgb, pal_.strongStroke.alpha * a));
        if (st_.editId != id || !st_.edit) {
            const std::wstring text = wide(value);
            if (text.empty()) drawText(placeholder, {b.x + 12, b.y, b.w - 24, b.h}, mono ? TextStyle::mono : TextStyle::body, pal_.text2.rgb, pal_.text2.alpha * a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            else drawText(text, {b.x + 12, b.y, b.w - 24, b.h}, mono ? TextStyle::mono : TextStyle::body, pal_.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    if (st_.editId == id) st_.editRect = b;  // the owner keeps the native control aligned
    return changed;
}

bool Frame::listRow(const std::string& id, bool selected, float height) {
    const Rect r = take(0, height);
    const bool hit = clicked(id, r);
    if (rt_) {
        const bool hot = hovered(r);
        if (selected || hot) fillRound(r, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha * (selected && hot ? 1.5f : 1));
        if (selected) selectionPill(r);
    }
    return hit;
}

bool Frame::navItem(const std::string& id, wchar_t g, std::wstring_view title, bool selected) {
    const Rect slot = take(0, 40);
    const Rect r{slot.x, slot.y + 2, slot.w, 36};
    const bool hit = clicked(id, r);
    if (rt_) {
        const bool hot = hovered(r);
        const bool down = st_.active == id;
        if (down) fillRound(r, kRadius, pal_.subtlePressed.rgb, pal_.subtlePressed.alpha);
        else if (selected || hot) fillRound(r, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha * (selected && hot ? 1.5f : 1));
        if (selected) selectionPill(r);
        glyph(g, {r.x + 12, r.y, 16, r.h}, pal_.text, down ? 0.7f : 1, 16);
        drawText(title, {r.x + 40, r.y, r.w - 48, r.h}, TextStyle::body, pal_.text, down ? 0.7f : 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    last_ = r;
    return hit;
}

// MARK: popups and finish

void Frame::drawPopups() {
    if (!rt_) return;  // input against popups is resolved at the start of the next pass (Frame::Frame)
    for (auto& p : st_.popups) {
        const Rect box = popupBox(p);
        // A flyout: a soft shadow, the flyout surface, a hair of stroke, rows with the accent pill on the current one.
        for (int i = 3; i >= 1; --i) {
            const float spread = static_cast<float>(i) * 2;
            rt_->FillRoundedRectangle(D2D1::RoundedRect({box.x - spread, box.y + spread, box.right() + spread, box.bottom() + spread * 2}, kFlyoutRadius + spread, kFlyoutRadius + spread),
                                      brush(0x000000, (pal_.isDark ? 0.10f : 0.05f) / static_cast<float>(i)));
        }
        fillRound(box, kFlyoutRadius, pal_.flyout, 1);
        strokeRound(box, kFlyoutRadius, pal_.flyoutStroke.rgb, pal_.flyoutStroke.alpha);
        for (std::size_t i = 0; i < p.options.size(); ++i) {
            const Rect row{box.x + kPopupPad, box.y + kPopupPad + kPopupRowH * static_cast<float>(i), box.w - 2 * kPopupPad, kPopupRowH};
            const bool on = p.current == static_cast<int>(i);
            const bool hot = row.contains(st_.mouseX, st_.mouseY);
            const Rect inner{row.x, row.y + 2, row.w, row.h - 4};
            if (hot || on) fillRound(inner, kRadius, pal_.subtleHover.rgb, pal_.subtleHover.alpha * (hot && on ? 1.5f : 1));
            if (on) selectionPill(inner);
            drawText(p.options[i], {row.x + 12, row.y, row.w - 24, row.h}, TextStyle::body, pal_.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
}

void Frame::finish() {
    drawPopups();
    if (!rt_ && (in_.kind == UiInput::Kind::up || in_.kind == UiInput::Kind::cancel)) st_.active.clear();  // no widget may keep a press past its end
    if (!rt_ && st_.anyDirty && st_.onChanged) st_.onChanged();
}

}  // namespace ixpat::ui
