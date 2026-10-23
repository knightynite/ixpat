#pragma once
#include <windows.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/d2d.h"
#include "ui/gfx_window.h"
#include "ui/theme.h"

namespace ixpat::ui {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    D2D1_RECT_F d2d() const { return D2D1::RectF(x, y, x + w, y + h); }
};

enum class TextStyle { title, body, bodyMedium, detail, footnote, footnoteSemibold, mono, monoSmall, caption, wordmark, heading };

/// Persistent state the immediate-mode toolkit keeps between frames.
struct UiState {
    std::string hot, active, focus;   // widget ids
    std::string openDropdown;         // a dropdown showing its popup
    float scrollY = 0, contentHeight = 0, viewHeight = 0;
    float mouseX = -1, mouseY = -1;
    bool consumed = false;            // this event was taken by a popup
    // native text editing
    HWND edit = nullptr;
    std::string editId;
    std::string* editTarget = nullptr;
    bool editCommitted = false;
    Rect editRect;                    // where the native edit sits this frame
    bool anyDirty = false;            // a widget changed a value this frame
    // drop-down popup to draw last (window space)
    struct Popup {
        std::string id;
        Rect anchor;
        std::vector<std::wstring> options;
        int* index = nullptr;
        float width = 0;
    };
    std::vector<Popup> popups;
    std::function<void()> onChanged;  // called once per frame when anyDirty
    std::function<void(HWND, const Rect&, const std::string&)> beginEdit;  // owner creates the native edit
};

/// One immediate-mode pass. Construct with the render target (painting) or nullptr (input), call the
/// widgets in layout order, then `finish()` to draw popups and run the change callback.
class Frame {
public:
    Frame(ID2D1RenderTarget* rt, const Palette& pal, const UiInput& in, UiState& st, float dpiScale, float winW, float winH);
    ~Frame();

    // Layout
    void begin(Rect area);               // vertical flow inside `area` (DIPs)
    void beginScroll(Rect viewport);     // scrollable region: content is offset by state.scrollY
    void endScroll();
    void beginRow(float height = 0);     // widgets flow horizontally until endRow
    void endRow();
    void space(float h);
    void gap(float w);                   // in a row
    float cursorX() const { return cx_; }
    float cursorY() const { return cy_; }
    float contentWidth() const { return area_.w; }
    void setX(float x) { cx_ = x; }
    Rect take(float w, float h);         // next widget rect in the current flow

    // Panels
    void beginCard(float padding = 18, bool accentTint = false);  // glass tile: draws background, indents
    void endCard();
    void backdrop();                     // the window backdrop gradient with glows

    // Text
    void label(std::wstring_view text, TextStyle style = TextStyle::body, float width = 0, std::uint32_t rgbOverride = 0, float alphaOverride = -1);
    float measureHeight(std::wstring_view text, TextStyle style, float width);
    void sectionTitle(std::wstring_view text);
    void divider(float alpha = 1);

    // Widgets (return true when the value changed / the button was clicked)
    bool toggle(const std::string& id, bool& value, std::wstring_view title = {}, std::wstring_view detail = {}, bool enabled = true);
    bool segmented(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled = true);
    bool slider(const std::string& id, double& value, double lo, double hi, double step, float width, std::wstring_view minLabel = {}, std::wstring_view maxLabel = {}, bool enabled = true);
    bool stepper(const std::string& id, int& value, int lo, int hi, int step, std::wstring_view text, float width = 120, bool enabled = true);
    bool button(const std::string& id, std::wstring_view text, bool prominent = false, bool enabled = true, float width = 0);
    bool iconButton(const std::string& id, wchar_t glyph, bool enabled = true, std::wstring_view tooltip = {});
    bool dropdown(const std::string& id, int& index, std::span<const std::wstring> options, float width, bool enabled = true);
    bool textField(const std::string& id, std::string& value, float width, std::wstring_view placeholder = {}, bool mono = false, bool enabled = true);
    bool pillTabs(const std::string& id, int& index, std::span<const std::wstring> titles);
    bool checkbox(const std::string& id, bool& value, std::wstring_view title);
    bool listRow(const std::string& id, bool selected, float height);  // returns clicked; draws selection; caller draws content at the rect
    Rect lastRect() const { return last_; }
    void dot(float cx, float cy, float r, std::uint32_t rgb, float alpha = 1, bool glow = false);
    void glyph(wchar_t g, const Rect& r, std::uint32_t rgb, float alpha, float size);
    void fillRound(const Rect& r, float radius, std::uint32_t rgb, float alpha);
    void strokeRound(const Rect& r, float radius, std::uint32_t rgb, float alpha, float width = 1);

    // Input
    bool painting() const { return rt_ != nullptr; }
    ID2D1RenderTarget* rt() const { return rt_; }
    const Palette& pal() const { return pal_; }
    const UiInput& input() const { return in_; }
    UiState& state() { return st_; }
    bool hovered(const Rect& r) const;
    bool clicked(const std::string& id, const Rect& r, bool enabled = true);  // press+release inside
    void finish();

    // Text formats (cached per style)
    IDWriteTextFormat* format(TextStyle style);
    void drawText(std::wstring_view text, const Rect& r, TextStyle style, std::uint32_t rgb, float alpha = 1, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR, bool wrap = true);
    float textWidth(std::wstring_view text, TextStyle style);

private:
    ID2D1SolidColorBrush* brush(std::uint32_t rgb, float alpha);
    std::uint32_t textColor(TextStyle style, float& alpha) const;
    void drawPopups();

    ID2D1RenderTarget* rt_;
    const Palette& pal_;
    UiInput in_;
    UiState& st_;
    float scale_;
    float winW_, winH_;
    Rect area_{};
    float cx_ = 0, cy_ = 0;
    bool inRow_ = false;
    float rowY_ = 0, rowH_ = 0, rowMaxH_ = 0;
    struct Saved {
        Rect area;
        float cx, cy;
        bool inRow;
        float rowY, rowH, rowMaxH;
    };
    std::vector<Saved> stack_;
    std::vector<Rect> cards_;
    std::vector<float> cardPadding_;
    std::vector<float>* cardHeights_ = nullptr;
    Rect scrollViewport_{};
    float scrollStartY_ = 0;
    bool inScroll_ = false;
    Rect last_{};
    Com<ID2D1SolidColorBrush> brush_;
    bool popupHandled_ = false;
};

/// Helper: a string id with an index suffix.
inline std::string idx(const char* base, int i) { return std::string(base) + "#" + std::to_string(i); }

}  // namespace ixpat::ui
