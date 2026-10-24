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

/// The Fluent type ramp, Segoe UI Variable: Display above 20 px, Text below. Nothing under 12 px.
///   pageTitle 28 semibold · subtitle 20 semibold · heading 16 semibold · title / bodyMedium 14 semibold (body strong)
///   body 14 · detail / caption 12 · footnote 12 (tertiary) · footnoteSemibold 12 semibold · mono / monoSmall 12 Cascadia
enum class TextStyle { title, body, bodyMedium, detail, footnote, footnoteSemibold, mono, monoSmall, caption, wordmark, heading, subtitle, pageTitle };

/// Persistent state the immediate-mode toolkit keeps between frames.
struct UiState {
    std::string hot, active, focus;   // widget ids
    bool focusVisible = false;        // the focus came from the keyboard: draw its ring (a click hides it, as Windows does)
    std::vector<std::string> focusOrder;  // the focusable widgets of the last pass, in layout order (Tab walks it)
    std::string focusScrollTo;        // bring this widget into the scroll viewport on the next pass
    std::string openDropdown;         // a dropdown showing its popup
    int popupHighlight = -1;          // the row the keyboard is on in the open popup (-1: none)
    int popupScroll = 0;              // the first row shown of a popup taller than the window
    float scrollY = 0, contentHeight = 0, viewHeight = 0;
    float mouseX = -1, mouseY = -1;
    bool consumed = false;            // this event was taken by a popup
    // native text editing
    HWND edit = nullptr;
    std::string editId;
    std::string* editTarget = nullptr;
    bool editMono = false;            // the field being edited shows a monospaced face
    bool editCommitted = false;
    Rect editRect;                    // where the native edit sits this frame
    bool anyDirty = false;            // a widget changed a value this frame
    // drop-down popup to draw last (window space)
    struct Popup {
        std::string id;
        Rect anchor;
        std::vector<std::wstring> options;
        int current = -1;
        float width = 0;
    };
    std::vector<Popup> popups;        // what the last pass showed; the next input pass hit-tests them first
    std::string pendingPopupId;       // a popup row was clicked: the dropdown with this id takes pendingPopupIndex
    int pendingPopupIndex = -1;
    bool rerun = false;               // the owner should run one more input pass (a pending selection waits)
    bool wheelPending = false;        // a wheel event inside the scroll viewport, applied at endScroll unless a child took it
    std::string wheelAccumId;         // the slider collecting wheel deltas, and how much of a notch it has so far
    float wheelAccum = 0;
    std::vector<float> cardHeights;   // each card's height from the previous pass, in order of appearance (drawn before its content)
    std::function<void()> onChanged;  // called once per frame when anyDirty
    std::function<void(HWND, const Rect&, const std::string&)> beginEdit;  // owner creates the native edit
};

/// One immediate-mode pass. Construct with the render target (painting) or nullptr (input), call the
/// widgets in layout order, then `finish()` to draw popups and run the change callback.
///
/// Keyboard: Tab and Shift+Tab walk the focusable widgets in layout order (the focus ring shows only then, as
/// Windows does); Space or Enter activates a button, toggle, check box, row or dropdown; Left/Right, Up/Down,
/// Home and End move sliders, selector bars and number boxes; in an open flyout Up/Down pick and Enter chooses;
/// Escape closes a flyout.
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
    float areaX() const { return area_.x; }
    void setX(float x) { cx_ = x; }
    Rect take(float w, float h);         // next widget rect in the current flow

    // Surfaces
    void layer(const Rect& r);           // the content area: a layer over the backdrop with its top-left corner rounded
    void beginCard(float padding = 16, bool accentTint = false);  // a settings card: fill, 1 px stroke, 4 px corners
    void endCard();
    /// A page as a stack of cards: beginSections opens the first card, sectionBreak closes it and opens the next,
    /// endSections closes the last. divider() inside stays a line within the card.
    void beginSections(float padding = 16);
    void sectionBreak();
    void endSections();

    // Text
    void label(std::wstring_view text, TextStyle style = TextStyle::body, float width = 0, std::uint32_t rgbOverride = 0, float alphaOverride = -1);
    float measureHeight(std::wstring_view text, TextStyle style, float width);
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
    bool checkbox(const std::string& id, bool& value, std::wstring_view title);
    bool listRow(const std::string& id, bool selected, float height);  // returns clicked; draws selection; caller draws content at the rect
    bool navItem(const std::string& id, wchar_t glyph, std::wstring_view title, bool selected);  // a navigation pane row
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
    bool clicked(const std::string& id, const Rect& r, bool enabled = true);  // press+release inside, or Space / Enter with the focus
    void finish();

    // Text formats (cached per style)
    IDWriteTextFormat* format(TextStyle style);
    void drawText(std::wstring_view text, const Rect& r, TextStyle style, std::uint32_t rgb, float alpha = 1, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR, bool wrap = true);
    float textWidth(std::wstring_view text, TextStyle style);

private:
    ID2D1SolidColorBrush* brush(std::uint32_t rgb, float alpha);
    std::uint32_t textColor(TextStyle style, float& alpha) const;
    void drawPopups();
    void controlSurface(const Rect& b, bool hot, bool down, bool enabled);  // a button-like control's fill and elevation border
    void selectionPill(const Rect& row);                                    // the 3 x 16 accent pill at a selected row's left edge
    // Keyboard
    void registerFocus(const std::string& id, const Rect& r, bool enabled);  // every interactive widget, in layout order
    bool focused(const std::string& id) const { return st_.focus == id; }
    bool activated(const std::string& id);                 // Space / Enter on the focused widget (consumed)
    int arrowStep(const std::string& id, bool horizontal);  // -1 / +1 for the focused widget's arrows (consumed), else 0
    int homeEnd(const std::string& id);                    // -1 Home / +1 End (consumed), else 0
    void focusRing(const Rect& r, float radius);
    int popupRowsVisible(const UiState::Popup& p) const;

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
    std::vector<std::size_t> cardIndex_;  // each open card's slot in st_.cardHeights
    std::size_t cardSeq_ = 0;
    bool inSections_ = false;
    float sectionPadding_ = 16;
    Rect scrollViewport_{};
    float scrollStartY_ = 0;
    bool inScroll_ = false;
    Rect last_{};
    std::vector<std::string> focusables_;
    Rect focusedRect_{};
    bool focusedSeen_ = false;
    Com<ID2D1SolidColorBrush> brush_;
    Rect popupBox(const UiState::Popup& p) const;
};

/// Helper: a string id with an index suffix.
inline std::string idx(const char* base, int i) { return std::string(base) + "#" + std::to_string(i); }

}  // namespace ixpat::ui
