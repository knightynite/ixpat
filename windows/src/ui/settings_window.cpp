#include "ui/settings_window.h"

#include <commctrl.h>

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/version.h"
#include "core/presets.h"
#include "ui/glyphs.h"

namespace ixpat::ui {
namespace {

constexpr UINT kEditId = 1001;

std::wstring W(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string N(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring fmt(const wchar_t* f, double v) {
    wchar_t b[64];
    _snwprintf_s(b, _TRUNCATE, f, v);
    return b;
}
std::wstring fmti(const wchar_t* f, int v) {
    wchar_t b[64];
    _snwprintf_s(b, _TRUNCATE, f, v);
    return b;
}

const std::vector<std::wstring> kTabs{L"Haptics", L"Action Button", L"Action Ring", L"Apps", L"Pointer & Scroll", L"Diagnostics"};
constexpr int kAboutTab = 6;  // reached from the header button and the tray menu, not a pill
const std::vector<std::wstring> kLevels{L"Off", L"Light", L"Medium", L"Strong", L"Max"};
const std::vector<std::wstring> kIntensities{L"1", L"2", L"3", L"4"};
const std::vector<std::wstring> kTri{L"Default", L"On", L"Off"};
const std::vector<std::wstring> kHoldActions{L"Do nothing", L"Context menu (right click)", L"Middle click", L"Action Ring"};

}  // namespace

SettingsWindow::SettingsWindow(Settings& settings, SettingsModel& model, SettingsActions actions)
    : settings_(settings), model_(model), actions_(std::move(actions)) {
    st_.onChanged = [this] { changed(); };
    st_.beginEdit = [this](HWND, const Rect& r, const std::string& id) { beginEdit(r, id, st_.editTarget, false); };
}

void SettingsWindow::create(HINSTANCE inst, HICON icon) {
    applyTheme();
    GfxWindow::create(inst, L"iXpat.Settings", L"iXpat", 860, 700, 760, 560, icon, true);
}

void SettingsWindow::applyTheme() { setPalette(resolve(settings_.theme, systemPrefersDark(), settings_.accentHex)); }

void SettingsWindow::refresh() { invalidate(); }

void SettingsWindow::pulse() {
    pulseStarts_.push_back(static_cast<double>(GetTickCount64()));
    animate(true);
}

bool SettingsWindow::consumeRerun() {
    const bool r = st_.rerun;
    st_.rerun = false;
    return r;
}

void SettingsWindow::cancelEditing() {
    cancelEdit();
    st_.openDropdown.clear();
    st_.popups.clear();
    st_.pendingPopupId.clear();
    pendingShortcut_.reset();
    recordingId_.clear();
    invalidate();
}

void SettingsWindow::onTimer() {
    const double now = static_cast<double>(GetTickCount64());
    std::erase_if(pulseStarts_, [&](double s) { return now - s > 700; });
    if (pulseStarts_.empty()) animate(false);
}

void SettingsWindow::changed() {
    if (actions_.changed) actions_.changed();
    invalidate();
}

// MARK: native text editing

void SettingsWindow::beginEdit(const Rect& r, const std::string& id, std::string* target, bool numeric) {
    commitEdit();
    if (!target) return;
    const float s = dpiScale();
    edit_ = CreateWindowExW(0, L"EDIT", W(*target).c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (numeric ? ES_NUMBER : 0),
                            static_cast<int>((r.x + 6) * s), static_cast<int>((r.y + 4) * s), static_cast<int>((r.w - 12) * s),
                            static_cast<int>((r.h - 8) * s), hwnd(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditId)),
                            GetModuleHandleW(nullptr), nullptr);
    if (!edit_) return;
    static HFONT font = nullptr;
    const int fontPx = static_cast<int>(-12 * s);
    if (font) DeleteObject(font);
    font = CreateFontW(fontPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    SetWindowSubclass(edit_, &SettingsWindow::editProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetFocus(edit_);
    editTarget_ = target;
    editId_ = id;
    editNumeric_ = numeric;
    st_.edit = edit_;
    st_.editId = id;
}

namespace {
constexpr UINT kEditCommandMsg = WM_APP + 60;  // wParam 1 = commit, 2 = cancel (posted from the EDIT subclass)
}

LRESULT CALLBACK SettingsWindow::editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<SettingsWindow*>(ref);
    switch (msg) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN || wp == VK_ESCAPE) {
                if (self) PostMessageW(self->hwnd(), kEditCommandMsg, wp == VK_RETURN ? 1 : 2, 0);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == L'\r' || wp == L'\n' || wp == 27) return 0;  // no beep for the keys handled above
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, &SettingsWindow::editProc, 1);
            break;
        default:
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void SettingsWindow::commitEdit() {
    if (!edit_) return;
    HWND h = edit_;
    edit_ = nullptr;  // detached first: DestroyWindow raises EN_KILLFOCUS synchronously
    st_.edit = nullptr;
    wchar_t buf[2048];
    GetWindowTextW(h, buf, 2048);
    const std::string text = N(buf);
    DestroyWindow(h);
    if (editTarget_ && *editTarget_ != text) {
        *editTarget_ = text;
        st_.editCommitted = true;
        UiInput none;
        build(nullptr, none);  // lets the field see the commit and mark the settings dirty
    }
    editTarget_ = nullptr;
    editId_.clear();
    st_.editId.clear();
    st_.focus.clear();
    if (GetActiveWindow() == hwnd()) SetFocus(hwnd());  // never pull focus back from another window
    invalidate();
}

void SettingsWindow::cancelEdit() {
    if (!edit_) return;
    HWND h = edit_;
    edit_ = nullptr;
    st_.edit = nullptr;
    DestroyWindow(h);
    editTarget_ = nullptr;
    editId_.clear();
    st_.editId.clear();
    st_.focus.clear();
    invalidate();
}

LRESULT SettingsWindow::onMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
    (void)lp;
    handled = false;
    if (msg == WM_COMMAND && LOWORD(wp) == kEditId && HIWORD(wp) == EN_KILLFOCUS) {
        if (edit_) commitEdit();
        handled = true;
        return 0;
    }
    if (msg == kEditCommandMsg) {
        if (wp == 1) commitEdit();
        else cancelEdit();
        handled = true;
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
        if (edit_) commitEdit();  // a click elsewhere commits first, then the click acts
        if (!recordingId_.empty()) {
            recordingId_.clear();
            invalidate();
        }
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !recordingId_.empty()) {
        // Shortcut recorder: the next key (with its modifiers) becomes the shortcut; Escape cancels.
        const int vk = static_cast<int>(wp);
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
            handled = true;  // a modifier pressed first: keep recording
            return 0;
        }
        if (vk != VK_ESCAPE) {
            std::uint64_t flags = 0;
            std::wstring label;
            if (GetKeyState(VK_CONTROL) & 0x8000) { flags |= kModCtrl; label += L"Ctrl+"; }
            if (GetKeyState(VK_MENU) & 0x8000) { flags |= kModAlt; label += L"Alt+"; }
            if (GetKeyState(VK_SHIFT) & 0x8000) { flags |= kModShift; label += L"Shift+"; }
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) { flags |= kModWin; label += L"Win+"; }
            wchar_t name[64]{};
            const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
            LONG lparam = static_cast<LONG>(scan << 16);
            if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_INSERT || vk == VK_DELETE) lparam |= 1 << 24;
            if (!GetKeyNameTextW(lparam, name, 64)) _snwprintf_s(name, _TRUNCATE, L"key %d", vk);
            label += name;
            // Apply to whichever action the recorder belongs to: stored as a pending record consumed by actionPicker.
            pendingShortcut_ = ButtonAction::blank(ActionKind::shortcut);
            pendingShortcut_->keyCode = vk;
            pendingShortcut_->flags = flags;
            pendingShortcut_->label = N(label);
        }
        recordingId_.clear();
        UiInput none;
        build(nullptr, none);  // the picker applies the recording inside an input pass, so it is saved and pushed once
        invalidate();
        handled = true;
        return 0;
    }
    return 0;
}

// MARK: build

void SettingsWindow::build(ID2D1RenderTarget* rt, const UiInput& in) {
    const Palette pal = resolve(settings_.theme, systemPrefersDark(), settings_.accentHex);
    if (pal.isDark != palette().isDark || pal.accent != palette().accent) setPalette(pal);
    Frame f(rt, palette(), in, st_, dpiScale(), widthDip(), heightDip());
    if (in.kind == UiInput::Kind::key && in.key == VK_ESCAPE && edit_) cancelEdit();
    if (in.kind == UiInput::Kind::key && in.key == VK_RETURN && edit_) commitEdit();
    f.backdrop();
    const float W0 = widthDip(), H0 = heightDip();
    f.begin({0, 0, W0, H0});
    header(f);
    const float headerBottom = f.cursorY();
    f.beginScroll({0, headerBottom, W0, H0 - headerBottom});
    f.begin({20, f.cursorY() + 8, W0 - 40, 100000});
    const float contentW = W0 - 40;
    // Hero + live tiles
    f.beginRow(300);
    heroTile(f, 330, 300);
    f.gap(16);
    liveTile(f, std::max(200.0f, contentW - 330 - 16), 300);
    f.endRow();
    f.space(16);
    int shown = tab_ < kAboutTab ? tab_ : -1;  // the About pane has no pill: none is lit
    if (f.pillTabs("tabs", shown, kTabs)) tab_ = shown;
    f.space(12);
    f.beginCard(18);
    switch (tab_) {
        case 0: hapticsPane(f); break;
        case 1: buttonPane(f); break;
        case 2: ringPane(f); break;
        case 3: appsPane(f); break;
        case 4: pointerPane(f); break;
        case kAboutTab: aboutPane(f); break;
        default: diagnosticsPane(f); break;
    }
    f.endCard();
    f.space(22);
    f.endScroll();
    f.finish();
    // Keep the native edit where its field is (it scrolls with the content).
    if (edit_ && st_.editId == editId_) {
        const float s = dpiScale();
        const Rect r = st_.editRect;
        SetWindowPos(edit_, nullptr, static_cast<int>((r.x + 6) * s), static_cast<int>((r.y + 4) * s), static_cast<int>((r.w - 12) * s), static_cast<int>((r.h - 8) * s), SWP_NOZORDER);
    }
}

void SettingsWindow::header(Frame& f) {
    const float W0 = widthDip();
    f.begin({20, 14, W0 - 40, 60});
    f.beginRow(40);
    // App glyph stands in for the icon image
    const Rect ic = f.take(34, 40);
    f.fillRound({ic.x, ic.y + 3, 34, 34}, 9, f.pal().accent, 0.9f);
    f.glyph(0xE962, {ic.x, ic.y + 3, 34, 34}, 0xFFFFFF, 1, 18);
    f.gap(12);
    const Rect tx = f.take(W0 - 40 - 34 - 12 - 60, 40);
    f.drawText(L"iXpat", {tx.x, tx.y - 4, 200, 30}, TextStyle::wordmark, f.pal().text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    f.drawText(L"Haptics, gestures and feel for the Surface Mouse on Windows", {tx.x, tx.y + 26, tx.w, 16}, TextStyle::detail, f.pal().text2.rgb, f.pal().text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    f.setX(20 + W0 - 40 - 40 - 44);
    if (f.iconButton("hdr/about", 0xE946, true, L"About iXpat")) tab_ = tab_ == kAboutTab ? 0 : kAboutTab;
    f.gap(8);
    f.toggle("master", settings_.enabled);
    f.endRow();
    f.space(10);
}

void SettingsWindow::showAbout() {
    tab_ = kAboutTab;
    refresh();
}

void SettingsWindow::heroTile(Frame& f, float width, float height) {
    const Rect r = f.take(width, height);
    f.fillRound(r, 18, f.pal().panel, f.pal().isDark ? 0.55f : 0.72f);
    f.fillRound(r, 18, f.pal().accent, f.pal().isDark ? 0.10f : 0.07f);
    f.strokeRound(r, 18, 0xFFFFFF, f.pal().isDark ? 0.18f : 0.6f);
    const Palette& pal = f.pal();
    f.drawText(L"SURFACE MOUSE · 2ND EDITION", {r.x + 14, r.y + 14, 200, 14}, TextStyle::footnoteSemibold, pal.text2.rgb, pal.text2.alpha);
    // Connection pill
    const std::wstring state = model_.device.connected ? L"Connected" : L"Offline";
    const float pw = f.textWidth(state, TextStyle::caption) + 30;
    const Rect pill{r.right() - 14 - pw, r.y + 11, pw, 20};
    f.fillRound(pill, 10, pal.field.rgb, pal.field.alpha);
    f.dot(pill.x + 11, pill.y + 10, 3.5f, model_.device.connected ? 0x30D158 : pal.text3.rgb, model_.device.connected ? 1 : pal.text3.alpha, model_.device.connected);
    f.drawText(state, {pill.x + 20, pill.y, pw - 24, 20}, TextStyle::caption, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    // Schematic
    SchematicState s;
    s.connected = model_.device.connected;
    s.buttons = model_.buttons;
    s.wheelTotal = model_.wheelTotal;
    s.actionPressed = model_.actionPressed;
    const double now = static_cast<double>(GetTickCount64());
    for (double start : pulseStarts_) s.pulseAges.push_back(static_cast<float>((now - start) / 1000.0));
    drawSchematic(f, {r.x, r.y + 30, r.w, r.h - 60}, s);
    // Battery + firmware
    const std::wstring batt = model_.device.battery ? fmti(L"%d%%", *model_.device.battery) : L"—";
    const wchar_t glyph = !model_.device.battery ? 0xE850 : *model_.device.battery >= 90 ? 0xE83F : *model_.device.battery >= 60 ? 0xE859 : *model_.device.battery >= 35 ? 0xE857 : *model_.device.battery >= 15 ? 0xE855 : 0xE850;
    f.glyph(glyph, {r.x + 14, r.bottom() - 32, 18, 18}, model_.device.battery && *model_.device.battery < 15 ? 0xFF453A : pal.text2.rgb, 1, 14);
    f.drawText(batt, {r.x + 36, r.bottom() - 32, 60, 18}, TextStyle::caption, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.drawText(L"fw " + W(model_.device.firmware), {r.right() - 114, r.bottom() - 32, 100, 18}, TextStyle::monoSmall, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void SettingsWindow::liveTile(Frame& f, float width, float height) {
    const Rect r = f.take(width, height);
    const Palette& pal = f.pal();
    f.fillRound(r, 18, pal.panel, pal.isDark ? 0.55f : 0.72f);
    f.strokeRound(r, 18, 0xFFFFFF, pal.isDark ? 0.18f : 0.6f);
    f.drawText(L"LIVE", {r.x + 14, r.y + 14, 100, 14}, TextStyle::footnoteSemibold, pal.text2.rgb, pal.text2.alpha);
    f.drawText(fmti(L"%d pulses", model_.pulseCount), {r.right() - 140, r.y + 14, 126, 14}, TextStyle::monoSmall, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto stat = [&](float y, const wchar_t* k, const std::string& v) {
        f.drawText(k, {r.x + 14, y, r.w - 28, 16}, TextStyle::footnoteSemibold, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        f.drawText(W(v), {r.x + 14, y + 18, r.w - 28, 20}, TextStyle::bodyMedium, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    };
    stat(r.y + 42, L"Last event", model_.lastEvent);
    stat(r.y + 90, L"Last pulse", model_.lastPlayed);
    stat(r.y + 138, L"Action button", model_.lastGesture);
    stat(r.y + 186, L"Front app", model_.frontApp.empty() ? "-" : model_.frontApp);
    // Status chips at the bottom
    auto chip = [&](float x, const std::wstring& text, bool ok) {
        const float cw = f.textWidth(text, TextStyle::footnote) + 24;
        const Rect c{x, r.bottom() - 36, cw, 22};
        f.fillRound(c, 11, ok ? 0x30D158 : 0xFF9F0A, 0.18f);
        f.dot(c.x + 10, c.y + 11, 3, ok ? 0x30D158 : 0xFF9F0A, 1, false);
        f.drawText(text, {c.x + 18, c.y, cw - 20, 22}, TextStyle::footnote, pal.text, 0.9f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        return cw;
    };
    float x = r.x + 14;
    x += chip(x, model_.input.mouseHook && model_.input.keyboardHook ? L"Hooks on" : L"Hooks off", model_.input.mouseHook && model_.input.keyboardHook) + 8;
    x += chip(x, model_.input.rawRegistered ? L"Raw input" : L"No raw input", model_.input.rawRegistered) + 8;
    chip(x, model_.bridgeRunning ? L"Bridge" : L"Bridge off", model_.bridgeRunning);
}

// MARK: Haptics pane

void SettingsWindow::feelPresets(Frame& f) {
    f.label(L"Feel presets", TextStyle::title);
    f.space(4);
    f.label(L"Pick a feel for each button and the wheel; the rows below are what it sets, and stay editable (the picker reads Custom once you change them).", TextStyle::detail);
    f.space(8);
    std::vector<std::wstring> clickNames, wheelNames;
    for (const auto& c : clickFeels()) clickNames.emplace_back(W(c.name));
    for (const auto& w : wheelFeels()) wheelNames.emplace_back(W(w.name));
    auto pickerFor = [&](const std::string& id, std::vector<std::wstring> names, const std::string& current, auto apply) {
        int index = -1;
        for (std::size_t i = 0; i < names.size(); ++i) if (N(names[i]) == current) index = static_cast<int>(i);
        if (index < 0) { names.emplace_back(L"Custom"); index = static_cast<int>(names.size()) - 1; }
        const int before = index;
        f.dropdown(id, index, names, 150);
        if (index != before && index < static_cast<int>(clickFeels().size()) + 1) apply(index);
    };
    // Lay the three pickers out on one row, labels above (drawn directly).
    {
        const float x0 = f.cursorX(), y0 = f.cursorY();
        f.beginRow(16);
        f.drawText(L"Left click", {x0, y0, 150, 16}, TextStyle::detail, f.pal().text2.rgb, f.pal().text2.alpha);
        f.drawText(L"Right click", {x0 + 160, y0, 150, 16}, TextStyle::detail, f.pal().text2.rgb, f.pal().text2.alpha);
        f.drawText(L"Scroll wheel", {x0 + 320, y0, 150, 16}, TextStyle::detail, f.pal().text2.rgb, f.pal().text2.alpha);
        f.endRow();
        f.beginRow(30);
        pickerFor("feel/left", clickNames, std::string(clickFeelName(settings_, false)), [&](int i) {
            if (i < static_cast<int>(clickFeels().size())) { applyClickFeel(settings_, clickFeels()[static_cast<std::size_t>(i)], false); st_.anyDirty = true; }
        });
        f.gap(10);
        pickerFor("feel/right", clickNames, std::string(clickFeelName(settings_, true)), [&](int i) {
            if (i < static_cast<int>(clickFeels().size())) { applyClickFeel(settings_, clickFeels()[static_cast<std::size_t>(i)], true); st_.anyDirty = true; }
        });
        f.gap(10);
        {
            int index = -1;
            std::vector<std::wstring> names = wheelNames;
            const std::string current(wheelFeelName(settings_));
            for (std::size_t i = 0; i < names.size(); ++i) if (N(names[i]) == current) index = static_cast<int>(i);
            if (index < 0) { names.emplace_back(L"Custom"); index = static_cast<int>(names.size()) - 1; }
            const int before = index;
            f.dropdown("feel/wheel", index, names, 150);
            if (index != before && index < static_cast<int>(wheelFeels().size())) { applyWheelFeel(settings_, wheelFeels()[static_cast<std::size_t>(index)]); st_.anyDirty = true; }
        }
        f.endRow();
    }
    f.space(6);
    std::wstring blurb = L"Left click: ";
    const std::string ln(clickFeelName(settings_, false)), wn(wheelFeelName(settings_));
    bool found = false;
    for (const auto& c : clickFeels()) if (c.name == ln) { blurb += W(c.blurb); found = true; }
    if (!found) blurb += L"custom rules.";
    blurb += L"   Wheel: ";
    found = false;
    for (const auto& w : wheelFeels()) if (w.name == wn) { blurb += W(w.blurb); found = true; }
    if (!found) blurb += L"custom rules.";
    f.label(blurb, TextStyle::detail);
}

void SettingsWindow::ruleRow(Frame& f, HapticEvent e) {
    HapticRule& rule = settings_.baseRule(e);
    const Palette& pal = f.pal();
    const float W1 = f.contentWidth();
    const float controlsW = 110 + 10 + 110 + 10 + 30 + 10;
    float extraW = 0;
    if (e == HapticEvent::scrollStep || e == HapticEvent::freeSpinTexture) extraW = 120;
    if (e == HapticEvent::leftHold) extraW = 140;
    if (rule.echo) extraW += 180;
    const float textW = std::max(200.0f, W1 - controlsW - extraW - 50);
    const float detailH = f.measureHeight(W(detail(e)), TextStyle::detail, textW);
    const float rowH = std::max(36.0f, 20 + detailH + 8);
    f.beginRow(rowH);
    f.toggle(idx("rule/on", static_cast<int>(e)), rule.enabled);
    f.gap(12);
    const Rect tr = f.take(textW, rowH);
    const float a = rule.enabled ? 1.0f : 0.6f;
    f.drawText(W(title(e)), {tr.x, tr.y + 2, tr.w, 18}, TextStyle::bodyMedium, pal.text, a, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    f.drawText(W(detail(e)), {tr.x, tr.y + 20, tr.w, detailH + 2}, TextStyle::detail, pal.text2.rgb, pal.text2.alpha * a);
    f.setX(tr.right() + 10);
    if (e == HapticEvent::scrollStep) f.stepper("rule/every", settings_.scrollStepEvery, 1, 10, 1, fmti(L"every %d", settings_.scrollStepEvery), 110);
    if (e == HapticEvent::freeSpinTexture) f.stepper("rule/texevery", settings_.freeSpinTextureEvery, 1, 6, 1, fmti(L"every %d", settings_.freeSpinTextureEvery), 110);
    if (e == HapticEvent::leftHold) f.stepper("rule/holdms", settings_.clickHoldMs, 150, 1500, 50, fmti(L"after %d ms", settings_.clickHoldMs), 130);
    if (extraW) f.gap(10);
    if (rule.echo) {
        std::string nm = "#" + std::to_string(rule.echo->ordinal);
        for (const auto& w : model_.device.table) if (w.ordinal == rule.echo->ordinal) nm = w.name();
        const std::wstring echo = L"+ " + W(nm) + L" " + std::to_wstring(rule.echo->intensity) + L" @" + std::to_wstring(rule.echo->delayMs) + L" ms";
        const Rect er = f.take(150, rowH);
        f.drawText(echo, {er.x, er.y, er.w, 30}, TextStyle::monoSmall, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        if (f.iconButton(idx("rule/echo-x", static_cast<int>(e)), 0xE711)) { rule.echo.reset(); st_.anyDirty = true; }
        f.gap(4);
    }
    // Waveform dropdown
    std::vector<std::wstring> names;
    int index = 0;
    for (std::size_t i = 0; i < model_.device.table.size(); ++i) {
        names.emplace_back(W(model_.device.table[i].name()));
        if (model_.device.table[i].ordinal == rule.ordinal) index = static_cast<int>(i);
    }
    const int before = index;
    f.dropdown(idx("rule/wave", static_cast<int>(e)), index, names, 110);
    if (index != before && index < static_cast<int>(model_.device.table.size())) { rule.ordinal = model_.device.table[static_cast<std::size_t>(index)].ordinal; st_.anyDirty = true; }
    f.gap(10);
    int level = std::clamp(rule.intensity, 1, 4) - 1;
    if (f.segmented(idx("rule/int", static_cast<int>(e)), level, kIntensities, 110)) rule.intensity = level + 1;
    f.gap(10);
    if (f.iconButton(idx("rule/test", static_cast<int>(e)), 0xE768, model_.device.connected) && actions_.preview) actions_.preview(rule, "Test");
    f.endRow();
    f.space(2);
}

void SettingsWindow::hapticsPane(Frame& f) {
    f.beginRow(30);
    f.toggle("haptics", settings_.hapticsEnabled, L"Haptic feedback");
    f.setX(f.contentWidth() + 18 - 300 - 84);
    const Rect lr = f.take(74, 30);
    f.drawText(L"Strength", lr, TextStyle::body, f.pal().text2.rgb, f.pal().text2.alpha, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.gap(10);
    f.segmented("master", settings_.masterIntensity, kLevels, 300);
    f.endRow();
    f.space(8);
    f.label(L"Each event below plays one of the waveforms the mouse itself reports (HID Simple Haptic Controller). The mouse's own click feel is in its firmware; these are extra pulses layered on top.", TextStyle::detail);
    f.divider();
    feelPresets(f);
    f.divider();
    for (std::size_t i = 0; i < kHapticEventCount; ++i) {
        ruleRow(f, static_cast<HapticEvent>(i));
        if (i + 1 < kHapticEventCount) f.divider(0.5f);
    }
}

// MARK: Action button pane

void SettingsWindow::actionPicker(Frame& f, const std::string& id, ButtonAction& action) {
    std::vector<std::wstring> kinds;
    for (int k = 0; k <= static_cast<int>(ActionKind::shell); ++k) kinds.emplace_back(W(title(static_cast<ActionKind>(k))));
    int index = static_cast<int>(action.kind);
    const int before = index;
    f.dropdown(id + "/kind", index, kinds, 180);
    if (index != before) { action = ButtonAction::blank(static_cast<ActionKind>(index)); st_.anyDirty = true; }
    f.gap(10);
    switch (action.kind) {
        case ActionKind::launchApp: {
            std::string name = action.path.empty() ? "Choose app..." : action.path.substr(action.path.find_last_of("\\/") + 1);
            if (f.button(id + "/choose", W(name)) && actions_.chooseApp) {
                if (auto p = actions_.chooseApp()) { action.path = *p; st_.anyDirty = true; }
            }
            break;
        }
        case ActionKind::shortcut: {
            const bool recording = recordingId_ == id;
            if (pendingShortcut_ && pendingTarget_ == id) {
                action.keyCode = pendingShortcut_->keyCode;
                action.flags = pendingShortcut_->flags;
                action.label = pendingShortcut_->label;
                pendingShortcut_.reset();
                pendingTarget_.clear();
                st_.anyDirty = true;
            }
            const std::wstring text = recording ? L"Press keys..." : (action.label.empty() ? L"Record shortcut" : W(action.label));
            if (f.button(id + "/record", text) && !f.painting()) {
                recordingId_ = id;
                pendingTarget_ = id;
                SetFocus(hwnd());
            }
            break;
        }
        case ActionKind::shell:
            f.textField(id + "/cmd", action.command, 220, L"PowerShell command", true);
            break;
        case ActionKind::actionRing:
            f.label(L"opens the ring (hold: sweep and release · press: click a chamber)", TextStyle::detail, 320);
            break;
        case ActionKind::gestures:
            f.label(L"hold, then move the mouse: ↑ ↓ ← → run the gesture actions below", TextStyle::detail, 360);
            break;
        default:
            break;
    }
}

void SettingsWindow::gesturePane(Frame& f) {
    f.label(L"Hold-and-move gestures", TextStyle::title);
    f.space(3);
    f.label(L"Keep the action button down and move the mouse. The pointer stays put; the first few pixels pick the axis. Up / down actions marked (step) repeat as you keep moving, with a tick each; sideways swipes fire once per swipe.", TextStyle::detail);
    f.space(8);
    auto row = [&](const wchar_t* label, const char* id, ButtonAction& a) {
        f.beginRow(30);
        const Rect lr = f.take(120, 30);
        f.drawText(label, lr, TextStyle::bodyMedium, f.pal().text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        actionPicker(f, id, a);
        f.endRow();
        f.space(4);
    };
    row(L"Move up", "gesture/up", settings_.gestureUp);
    row(L"Move down", "gesture/down", settings_.gestureDown);
    row(L"Move left", "gesture/left", settings_.gestureLeft);
    row(L"Move right", "gesture/right", settings_.gestureRight);
    f.beginRow(28);
    f.drawText(L"Step size", f.take(120, 28), TextStyle::body, f.pal().text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.slider("gesture/step", settings_.gestureStepPx, 8, 80, 2, 300, L"Fine", L"Coarse");
    f.gap(10);
    f.drawText(fmti(L"%d px", static_cast<int>(settings_.gestureStepPx)), f.take(60, 28), TextStyle::mono, f.pal().text2.rgb, f.pal().text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.endRow();
    f.beginRow(28);
    f.drawText(L"Swipe length", f.take(120, 28), TextStyle::body, f.pal().text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.slider("gesture/swipe", settings_.gestureSwipePx, 30, 250, 10, 300, L"Short", L"Long");
    f.gap(10);
    f.drawText(fmti(L"%d px", static_cast<int>(settings_.gestureSwipePx)), f.take(60, 28), TextStyle::mono, f.pal().text2.rgb, f.pal().text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.endRow();
    f.toggle("gesture/freeze", settings_.gestureFreezePointer, L"Freeze the pointer while gesturing");
}

void SettingsWindow::buttonPane(Frame& f) {
    const Palette& pal = f.pal();
    f.label(L"How the mouse reports its action button", TextStyle::title);
    f.space(3);
    f.label(L"The firmware decides the gesture and sends a Windows chord for each: press = Win+Shift+S (Snipping Tool), double press = Win + a fake left click (Click to Do), hold = Win+Shift+F23, the Copilot key, held. iXpat maps those chords straight to the actions below and hides them from your apps.", TextStyle::detail);
    f.space(4);
    f.beginRow(18);
    f.label(L"Last gesture:", TextStyle::detail);
    f.gap(6);
    f.label(W(model_.lastGesture), TextStyle::detail, 0, pal.accent, 1);
    f.endRow();
    f.divider();
    const wchar_t* names[3] = {L"Press", L"Double press", L"Press and hold"};
    for (int g = 0; g < 3; ++g) {
        f.beginRow(30);
        f.drawText(names[g], f.take(120, 30), TextStyle::bodyMedium, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        ButtonAction& a = settings_.action(static_cast<ButtonGesture>(g));
        actionPicker(f, idx("action", g), a);
        f.setX(f.contentWidth() + 18 - 34);
        if (f.iconButton(idx("action/run", g), 0xE768) && actions_.run) actions_.run(a, g == 2);
        f.endRow();
        f.space(6);
    }
    f.divider();
    f.toggle("gesturesOnHold", settings_.gesturesOnHold, L"Hold-and-move gestures on every hold",
             L"Keep the button down and move to gesture; release without moving and the hold action above runs as usual. Off: only a hold mapped to “Hold-and-move gestures” gestures.");
    bool anyGestures = settings_.gesturesOnHold;
    for (int g = 0; g < 3; ++g) anyGestures |= settings_.action(static_cast<ButtonGesture>(g)).kind == ActionKind::gestures;
    if (anyGestures) { f.space(8); gesturePane(f); }
    f.divider();
    f.toggle("holdAction/on", settings_.forceClick, L"Press-and-hold action on the left button",
             L"Keep the left button down without moving and iXpat runs the action below with a deeper second click on the mouse (the Mac's force click). Release or move first and nothing extra happens.");
    f.space(4);
    f.beginRow(30);
    f.drawText(L"Hold does", f.take(130, 30), TextStyle::body, pal.text, settings_.forceClick ? 1 : 0.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    int hold = static_cast<int>(settings_.windows.holdAction);
    if (f.dropdown("holdAction/kind", hold, kHoldActions, 240, settings_.forceClick)) {}
    if (hold != static_cast<int>(settings_.windows.holdAction)) { settings_.windows.holdAction = static_cast<HoldAction>(hold); st_.anyDirty = true; }
    f.endRow();
    f.beginRow(28);
    f.drawText(L"Hold for", f.take(130, 28), TextStyle::body, pal.text, settings_.forceClick ? 1 : 0.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    double ms = settings_.forceClickMs;
    if (f.slider("holdAction/ms", ms, 200, 800, 25, 300, L"Quick", L"Patient", settings_.forceClick)) settings_.forceClickMs = static_cast<int>(ms);
    f.gap(10);
    f.drawText(fmti(L"%d ms", settings_.forceClickMs), f.take(60, 28), TextStyle::mono, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.endRow();
    f.divider();
    f.toggle("swallow", settings_.swallowButton, L"Hide the button's built-in chords from apps",
             L"Otherwise a press also reaches the foreground app as Win+Shift+S (Snipping Tool) and the double press as a real click.");
}

// MARK: Action Ring pane

void SettingsWindow::ringEditor(Frame& f, const std::string& id, std::vector<RingItem>& items) {
    const Palette& pal = f.pal();
    int remove = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        f.beginRow(34);
        const Rect ic = f.take(34, 34);
        f.fillRound(ic, 17, pal.accent, 0.18f);
        const wchar_t g = glyphFor(items[i].symbol);
        if (g) f.glyph(g, ic, pal.text, 1, 16);
        else f.drawText(items[i].title.empty() ? L"?" : W(items[i].title.substr(0, 1)), ic, TextStyle::bodyMedium, pal.text, 1, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        f.gap(10);
        f.textField(id + "/title/" + items[i].id, items[i].title, 130, L"Title");
        f.gap(10);
        f.textField(id + "/symbol/" + items[i].id, items[i].symbol, 150, L"Symbol", true);
        f.gap(10);
        actionPicker(f, id + "/act/" + items[i].id, items[i].action);
        f.setX(f.contentWidth() + 18 - 34);
        if (f.iconButton(id + "/rm/" + items[i].id, 0xE738, items.size() > 2)) remove = static_cast<int>(i);
        f.endRow();
        f.space(6);
    }
    if (remove >= 0) { items.erase(items.begin() + remove); st_.anyDirty = true; }
    f.beginRow(30);
    if (f.button(id + "/add", L"+ Add chamber", false, items.size() < 8)) { items.push_back({newUuidUpper(), "New", "star.fill", ButtonAction{}}); st_.anyDirty = true; }
    f.gap(10);
    if (f.button(id + "/reset", L"Reset to defaults")) { items = defaultRingItems(); st_.anyDirty = true; }
    f.gap(16);
    f.label(L"Symbol names are the Mac's SF Symbols (e.g. terminal, lock.fill, star.fill); unknown ones show the title's first letter.", TextStyle::footnote, 420);
    f.endRow();
}

void SettingsWindow::ringPane(Frame& f) {
    const std::wstring ringIntro = L"A ring of shortcuts that opens under the pointer. Assign it to the action button (press or hold). Sweep the mouse toward a chamber (you feel a detent per chamber), then release or click to run it. Escape or right-click cancels.";
    const float introH = f.measureHeight(ringIntro, TextStyle::detail, f.contentWidth() - 160);
    f.beginRow(24 + introH);
    const Rect tr = f.take(f.contentWidth() - 150, 24 + introH);
    f.drawText(L"Action Ring", {tr.x, tr.y, tr.w, 20}, TextStyle::title, f.pal().text);
    f.drawText(ringIntro, {tr.x, tr.y + 22, tr.w, introH + 2}, TextStyle::detail, f.pal().text2.rgb, f.pal().text2.alpha);
    f.gap(10);
    if (f.button("ring/open", L"Open ring now", true) && actions_.openRing) actions_.openRing();
    f.endRow();
    f.divider();
    ringEditor(f, "ring", settings_.ringItems);
}

// MARK: Apps pane

void SettingsWindow::appsPane(Frame& f) {
    const Palette& pal = f.pal();
    const std::wstring appsIntro = L"Overrides that apply while an app is in front: a different click or wheel feel, haptics off, its own ring, its own gestures. Anything left on Default follows the main tabs.";
    const float introH = f.measureHeight(appsIntro, TextStyle::detail, f.contentWidth() - 150);
    f.beginRow(24 + introH);
    const Rect tr = f.take(f.contentWidth() - 140, 24 + introH);
    f.drawText(L"Per-app profiles", {tr.x, tr.y, tr.w, 20}, TextStyle::title, pal.text);
    f.drawText(appsIntro, {tr.x, tr.y + 22, tr.w, introH + 2}, TextStyle::detail, pal.text2.rgb, pal.text2.alpha);
    f.gap(10);
    if (f.button("apps/add", L"+ Add app", true, true, 120) && !f.painting()) {
        runningApps_ = actions_.runningApps ? actions_.runningApps() : std::vector<std::pair<std::string, std::string>>{};
        runningAppsMenu_ = true;
    }
    f.endRow();
    if (runningAppsMenu_) {
        // A simple list of running apps to pick from, plus a file chooser.
        f.space(8);
        f.beginCard(10);
        for (std::size_t i = 0; i < runningApps_.size() && i < 12; ++i) {
            if (f.listRow(idx("apps/pick", static_cast<int>(i)), false, 26)) {
                std::string image = runningApps_[i].first;
                for (auto& c : image) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                bool exists = false;
                for (const auto& p : settings_.profiles) if (p.bundleID == image) { exists = true; selectedProfile_ = p.id; }
                if (!exists) {
                    AppProfile p;
                    p.id = newUuidUpper();
                    p.bundleID = image;
                    p.name = runningApps_[i].second.empty() ? image : runningApps_[i].second;
                    settings_.profiles.push_back(p);
                    selectedProfile_ = p.id;
                    st_.anyDirty = true;
                }
                runningAppsMenu_ = false;
            }
            const Rect r = f.lastRect();
            f.drawText(W(runningApps_[i].second), {r.x + 8, r.y, r.w - 220, r.h}, TextStyle::body, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            f.drawText(W(runningApps_[i].first), {r.right() - 208, r.y, 200, r.h}, TextStyle::monoSmall, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
        f.beginRow(30);
        if (f.button("apps/choose", L"Choose a program...") && actions_.chooseExe && !f.painting()) {
            if (auto p = actions_.chooseExe()) {
                std::string image = p->substr(p->find_last_of("\\/") + 1);
                for (auto& c : image) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                AppProfile prof;
                prof.id = newUuidUpper();
                prof.bundleID = image;
                prof.name = image;
                settings_.profiles.push_back(prof);
                selectedProfile_ = prof.id;
                st_.anyDirty = true;
            }
            runningAppsMenu_ = false;
        }
        f.gap(10);
        if (f.button("apps/cancel", L"Cancel")) runningAppsMenu_ = false;
        f.endRow();
        f.endCard();
    }
    f.divider();
    if (settings_.profiles.empty()) {
        f.label(L"No profiles yet. Add the apps you want to feel different: a design tool with a fine wheel, a game with haptics off, a DAW with its own ring.", TextStyle::body);
        return;
    }
    if (!selectedProfile_) selectedProfile_ = settings_.profiles.front().id;
    // Left: profile list; right: editor
    const float listW = 230;
    const float fullW = f.contentWidth();  // before the list sub-area narrows the frame
    f.beginRow(0);
    const float x0 = f.cursorX(), y0 = f.cursorY();
    f.endRow();
    // list (vertical flow in a sub-area)
    f.begin({x0, y0, listW, 100000});
    int removeAt = -1;
    for (std::size_t i = 0; i < settings_.profiles.size(); ++i) {
        const auto& p = settings_.profiles[i];
        if (f.listRow("apps/row/" + p.id, selectedProfile_ == p.id, 40)) selectedProfile_ = p.id;
        const Rect r = f.lastRect();
        f.glyph(0xE7FC, {r.x + 6, r.y + 8, 24, 24}, pal.accent, 1, 16);
        f.drawText(W(p.name), {r.x + 36, r.y + 4, r.w - 44, 18}, TextStyle::bodyMedium, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        f.drawText(W(p.bundleID), {r.x + 36, r.y + 22, r.w - 44, 14}, TextStyle::footnote, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        if (settings_.frontBundleID == p.bundleID) f.dot(r.right() - 12, r.y + 20, 3, 0x30D158, 1, false);
        f.space(4);
    }
    const float listBottom = f.cursorY();
    // editor
    f.begin({x0 + listW + 16, y0, fullW - listW - 16, 100000});
    auto it = std::find_if(settings_.profiles.begin(), settings_.profiles.end(), [&](const AppProfile& p) { return p.id == *selectedProfile_; });
    if (it == settings_.profiles.end()) {
        f.label(L"Select an app on the left.", TextStyle::body);
    } else {
        AppProfile& p = *it;
        f.beginRow(30);
        f.drawText(W(p.name), f.take(f.contentWidth() - 110, 30), TextStyle::heading, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        if (f.button("apps/remove", L"Remove")) removeAt = static_cast<int>(it - settings_.profiles.begin());
        f.endRow();
        f.space(8);
        auto tri = [&](const char* id, const wchar_t* label, std::optional<bool>& v) {
            f.beginRow(30);
            f.drawText(label, f.take(120, 30), TextStyle::body, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            int index = !v ? 0 : (*v ? 1 : 2);
            if (f.segmented(std::string("apps/tri/") + id, index, kTri, 170)) { if (index == 0) v.reset(); else v = index == 1; }
            f.endRow();
        };
        tri("haptics", L"Haptics", p.hapticsEnabled);
        tri("smooth", L"Smooth scrolling", p.smoothScroll);
        tri("freespin", L"Free-spin", p.freeSpin);
        tri("dynamic", L"Dynamic ratchet", p.ratchetDynamic);
        // Click feel pickers for the profile
        auto profileClick = [&](const char* id, const wchar_t* label, bool right) {
            const std::string d = std::string(rawName(right ? HapticEvent::rightDown : HapticEvent::leftDown));
            const std::string u = std::string(rawName(right ? HapticEvent::rightUp : HapticEvent::leftUp));
            const std::string h = std::string(rawName(right ? HapticEvent::rightHold : HapticEvent::leftHold));
            std::vector<std::wstring> names{L"Default"};
            for (const auto& c : clickFeels()) names.emplace_back(W(c.name));
            int index = 0;
            if (p.rules.count(d)) {
                index = static_cast<int>(names.size());  // Custom unless matched
                for (std::size_t i = 0; i < clickFeels().size(); ++i) {
                    const auto& c = clickFeels()[i];
                    auto get = [&](const std::string& k) -> std::optional<HapticRule> { auto f2 = p.rules.find(k); return f2 == p.rules.end() ? std::nullopt : std::optional<HapticRule>(f2->second); };
                    if (get(d) == c.down && get(u) == c.up && get(h) == c.hold) index = static_cast<int>(i) + 1;
                }
                if (index == static_cast<int>(names.size())) names.emplace_back(L"Custom");
            }
            f.beginRow(30);
            f.drawText(label, f.take(120, 30), TextStyle::body, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            const int before = index;
            f.dropdown(std::string("apps/click/") + id, index, names, 170);
            if (index != before) {
                if (index == 0) { p.rules.erase(d); p.rules.erase(u); p.rules.erase(h); }
                else if (index - 1 < static_cast<int>(clickFeels().size())) { const auto& c = clickFeels()[static_cast<std::size_t>(index - 1)]; p.rules[d] = c.down; p.rules[u] = c.up; p.rules[h] = c.hold; }
                st_.anyDirty = true;
            }
            f.endRow();
        };
        profileClick("left", L"Left click", false);
        profileClick("right", L"Right click", true);
        {
            const std::string sk = std::string(rawName(HapticEvent::scrollStep)), tk = std::string(rawName(HapticEvent::freeSpinTexture));
            std::vector<std::wstring> names{L"Default"};
            for (const auto& w : wheelFeels()) names.emplace_back(W(w.name));
            int index = 0;
            if (p.rules.count(sk)) {
                index = static_cast<int>(names.size());
                for (std::size_t i = 0; i < wheelFeels().size(); ++i) {
                    const auto& w = wheelFeels()[i];
                    auto get = [&](const std::string& k) -> std::optional<HapticRule> { auto f2 = p.rules.find(k); return f2 == p.rules.end() ? std::nullopt : std::optional<HapticRule>(f2->second); };
                    if (get(sk) == w.step && p.scrollStepEvery == w.every && p.ratchetDynamic == w.dynamic && p.ratchetLightRate == w.lightRate && p.freeSpin == w.freeSpin && get(tk) == w.texture && p.freeSpinTextureEvery == w.textureEvery) index = static_cast<int>(i) + 1;
                }
                if (index == static_cast<int>(names.size())) names.emplace_back(L"Custom");
            }
            f.beginRow(30);
            f.drawText(L"Wheel", f.take(120, 30), TextStyle::body, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            const int before = index;
            f.dropdown("apps/wheel", index, names, 170);
            if (index != before) {
                if (index == 0) { p.rules.erase(sk); p.rules.erase(tk); p.scrollStepEvery.reset(); p.ratchetDynamic.reset(); p.ratchetLightRate.reset(); p.freeSpin.reset(); p.freeSpinTextureEvery.reset(); }
                else if (index - 1 < static_cast<int>(wheelFeels().size())) { const auto& w = wheelFeels()[static_cast<std::size_t>(index - 1)]; p.rules[sk] = w.step; p.rules[tk] = w.texture; p.scrollStepEvery = w.every; p.ratchetDynamic = w.dynamic; p.ratchetLightRate = w.lightRate; p.freeSpin = w.freeSpin; p.freeSpinTextureEvery = w.textureEvery; }
                st_.anyDirty = true;
            }
            f.endRow();
        }
        f.divider();
        bool ownGestures = p.gestureUp.has_value();
        if (f.toggle("apps/gestures", ownGestures, L"Own hold-and-move gestures")) {
            if (ownGestures) { p.gestureUp = settings_.gestureUp; p.gestureDown = settings_.gestureDown; p.gestureLeft = settings_.gestureLeft; p.gestureRight = settings_.gestureRight; }
            else { p.gestureUp.reset(); p.gestureDown.reset(); p.gestureLeft.reset(); p.gestureRight.reset(); }
        }
        if (p.gestureUp) {
            auto row = [&](const wchar_t* label, const char* id, std::optional<ButtonAction>& a) {
                if (!a) a = ButtonAction{};
                f.beginRow(30);
                f.drawText(label, f.take(60, 30), TextStyle::detail, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
                actionPicker(f, std::string("apps/g/") + id, *a);
                f.endRow();
                f.space(4);
            };
            row(L"Up", "up", p.gestureUp);
            row(L"Down", "down", p.gestureDown);
            row(L"Left", "left", p.gestureLeft);
            row(L"Right", "right", p.gestureRight);
        }
        f.divider();
        bool ownRing = p.ringItems.has_value();
        if (f.toggle("apps/ring", ownRing, L"Own Action Ring")) {
            if (ownRing) p.ringItems = settings_.ringItems;
            else p.ringItems.reset();
        }
        if (p.ringItems) ringEditor(f, "apps/ringed", *p.ringItems);
    }
    const float editorBottom = f.cursorY();
    f.begin({x0, std::max(listBottom, editorBottom), f.contentWidth(), 100000});
    if (removeAt >= 0) {
        settings_.profiles.erase(settings_.profiles.begin() + removeAt);
        selectedProfile_.reset();
        st_.anyDirty = true;
    }
}

// MARK: Pointer & scroll pane

void SettingsWindow::pointerPane(Frame& f) {
    const Palette& pal = f.pal();
    auto sliderRow = [&](const wchar_t* label, const char* id, double& v, double lo, double hi, double step, const wchar_t* lo_l, const wchar_t* hi_l, const std::wstring& value, bool enabled) {
        f.beginRow(28);
        f.drawText(label, f.take(130, 28), TextStyle::body, pal.text, enabled ? 1 : 0.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        f.slider(id, v, lo, hi, step, 330, lo_l, hi_l, enabled);
        f.gap(10);
        f.drawText(value, f.take(60, 28), TextStyle::mono, pal.text2.rgb, pal.text2.alpha * (enabled ? 1 : 0.5f), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        f.endRow();
    };
    f.toggle("ptr/manage", settings_.managePointer, L"Manage pointer settings for this mouse",
             L"Applies while the Surface Mouse is the device moving the pointer; another mouse or the touchpad gets the system values back. Re-applied whenever the mouse reconnects.");
    sliderRow(L"Tracking speed", "ptr/speed", settings_.pointerSpeed, 0, 3, 0.125, L"Slow", L"Fast", fmt(L"%.2f", settings_.pointerSpeed), settings_.managePointer);
    f.toggle("ptr/noaccel", settings_.noAcceleration, L"No pointer acceleration", L"Turns off “Enhance pointer precision” while this mouse drives the pointer: 1:1 sensor movement.", settings_.managePointer);
    sliderRow(L"Scrolling speed", "ptr/scroll", settings_.scrollSpeed, 0.05, 1.5, 0.05, L"Slow", L"Fast", fmt(L"%.2f", settings_.scrollSpeed), settings_.managePointer);
    f.divider();
    f.toggle("ptr/reverse", settings_.reverseScroll, L"Reverse scrolling direction for this mouse", L"Flips wheel scrolls only; the touchpad keeps its own setting.");
    f.divider();
    f.label(L"Wheel feel", TextStyle::title);
    f.space(6);
    f.toggle("ptr/smooth", settings_.smoothScroll, L"Smooth kinetic scrolling", L"Each notch becomes a pixel glide instead of a jump; a fast spin coasts on after the wheel stops.");
    const bool sm = settings_.smoothScroll;
    sliderRow(L"Distance per notch", "ptr/px", settings_.scrollPixelsPerNotch, 20, 160, 5, L"", L"", fmti(L"%d px", static_cast<int>(settings_.scrollPixelsPerNotch)), sm);
    sliderRow(L"Glide", "ptr/glide", settings_.scrollGlide, 1, 10, 1, L"Snappy", L"Long", fmti(L"%d", static_cast<int>(settings_.scrollGlide)), sm);
    sliderRow(L"Momentum", "ptr/momentum", settings_.scrollMomentum, 0, 10, 1, L"Off", L"Coast", fmti(L"%d", static_cast<int>(settings_.scrollMomentum)), sm);
    sliderRow(L"Acceleration", "ptr/accel", settings_.scrollAccel, 0, 3, 0.25, L"Linear", L"Fast", fmt(L"%.2f×", 1 + settings_.scrollAccel), sm);
    HapticRule& stepRule = settings_.baseRule(HapticEvent::scrollStep);
    f.toggle("ptr/ratchet", stepRule.enabled, L"Ratchet tick per notch", L"A detent pulse for every notch, so the free-spinning wheel feels notched. Waveform and strength are in the Haptics tab.");
    f.toggle("ptr/dynamic", settings_.ratchetDynamic, L"Dynamic ratchet", L"Firm detents when you scroll slowly, light quick ticks when you scroll fast, a firmer bump when you reverse.", stepRule.enabled);
    sliderRow(L"Go light above", "ptr/light", settings_.ratchetLightRate, 4, 30, 1, L"", L"", fmti(L"%d/s", static_cast<int>(settings_.ratchetLightRate)), settings_.ratchetDynamic && stepRule.enabled);
    f.toggle("ptr/freespin", settings_.freeSpin, L"Free-spin on a flick", L"Spin fast and the wheel switches to free-spin: no ticks, boosted distance, one Collide pulse on the switch.");
    sliderRow(L"Flick threshold", "ptr/flick", settings_.freeSpinThreshold, 8, 40, 1, L"", L"", fmti(L"%d/s", static_cast<int>(settings_.freeSpinThreshold)), settings_.freeSpin);
    sliderRow(L"Free-spin boost", "ptr/boost", settings_.freeSpinMultiplier, 1.5, 6, 0.5, L"", L"", fmt(L"%.1f×", settings_.freeSpinMultiplier), settings_.freeSpin);
    f.divider();
    f.label(L"System", TextStyle::title);
    f.space(6);
    f.toggle("sys/login", settings_.windows.startAtLogin, L"Start iXpat at login", L"Adds iXpat to your account's startup programs (HKCU Run). Task Manager's Startup tab can veto it.");
    f.toggle("sys/updates", settings_.autoCheckUpdates, L"Check for updates daily", L"Asks GitHub for a newer Windows release once a day. Nothing is downloaded or installed by itself.");
    f.divider();
    f.beginRow(30);
    f.label(L"Double-click speed, button swap and the system-wide pointer speed live in Windows Settings.", TextStyle::detail, f.contentWidth() - 220);
    f.setX(f.contentWidth() + 18 - 200);
    if (f.button("ptr/open", L"Open Windows mouse settings", false, true, 200) && actions_.openMouseSettings) actions_.openMouseSettings();
    f.endRow();
}

// MARK: About pane (AboutView.swift)
void SettingsWindow::aboutPane(Frame& f) {
    const Palette& pal = f.pal();
    const float cw = f.contentWidth();
    const Rect ic = f.take(0, 88);
    f.fillRound({ic.x + cw / 2 - 44, ic.y, 88, 88}, 22, pal.accent, 0.9f);
    f.glyph(0xE962, {ic.x + cw / 2 - 44, ic.y, 88, 88}, 0xFFFFFF, 1, 46);
    f.space(10);
    f.drawText(L"iXpat", f.take(0, 34), TextStyle::wordmark, pal.text, 1, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
#if defined(_M_ARM64)
    const wchar_t* arch = L"arm64";
#else
    const wchar_t* arch = L"x64";
#endif
    f.drawText(L"Version " IXPAT_VERSION_WSTRING L" · " + std::wstring(arch) + L" · Windows", f.take(0, 18), TextStyle::detail, pal.text2.rgb, pal.text2.alpha,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.space(8);
    f.drawText(L"Haptics, an Action Ring, kinetic scrolling and force click for the Microsoft Surface Mouse (2nd Edition) on Windows.",
               f.take(0, 40), TextStyle::body, pal.text, 1, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
    f.space(6);
    f.drawText(L"Created by", f.take(0, 14), TextStyle::footnote, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.space(4);
    f.beginRow(28);
    f.setX(f.areaX() + cw / 2 - 150);
    if (f.button("about/author", L"AL Najafi", false, true, 120) && actions_.openUrl) actions_.openUrl("https://www.linkedin.com/in/alnnajafi/");
    f.gap(8);
    if (f.button("about/mac", L"Mac app", false, true, 90) && actions_.openUrl) actions_.openUrl("https://github.com/knightynite/ixpat#install");
    f.gap(8);
    if (f.button("about/measure", L"Measurements", false, true, 118) && actions_.openUrl) actions_.openUrl("https://github.com/knightynite/ixpat/blob/main/windows/docs/measurements-2026-10.md");
    f.endRow();
    f.space(6);
    f.beginRow(28);
    f.setX(f.areaX() + cw / 2 - 180);
    if (f.button("about/site", L"Website", false, true, 110) && actions_.openUrl) actions_.openUrl("https://knightynite.github.io/ixpat/");
    f.gap(8);
    if (f.button("about/source", L"Source code", false, true, 120) && actions_.openUrl) actions_.openUrl("https://github.com/knightynite/ixpat");
    f.gap(8);
    if (f.button("about/issues", L"Report an issue", false, true, 130) && actions_.openUrl) actions_.openUrl("https://github.com/knightynite/ixpat/issues");
    f.endRow();
    f.space(12);
    f.beginCard(12, true);
    f.toggle("about/auto", settings_.autoCheckUpdates, L"Check for updates daily");
    f.beginRow(30);
    const std::wstring status = model_.updateStatus.empty() ? std::wstring() : W(model_.updateStatus);
    f.drawText(status, f.take(cw - 24 - 130 - 8 - 150 - 8, 30), TextStyle::detail, model_.updateAvailable ? pal.accent : pal.text2.rgb,
               model_.updateAvailable ? 1 : pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.gap(8);
    if (model_.updateAvailable) {
        if (f.button("about/open", L"Open release page", true, true, 150) && actions_.openUrl) actions_.openUrl(model_.updateAvailable->url);
        f.gap(8);
    }
    if (f.button("about/check", model_.updateChecking ? L"Checking..." : L"Check now", false, !model_.updateChecking, 130) && actions_.checkUpdates)
        actions_.checkUpdates();
    f.endRow();
    f.endCard();
    f.space(10);
    f.drawText(L"Free and open source under the MIT License. Not affiliated with or endorsed by Microsoft. Surface is a trademark of Microsoft Corporation. Windows builds are unsigned: updates open the release page instead of installing themselves.",
               f.take(0, 34), TextStyle::footnote, pal.text3.rgb, pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
}

// MARK: Diagnostics pane

void SettingsWindow::bridgeSection(Frame& f) {
    const Palette& pal = f.pal();
    f.beginRow(24);
    const Rect tick = f.take(3, 24);
    f.fillRound({tick.x, tick.y + 5, 3, 14}, 1.5f, pal.accent, 1);
    f.gap(8);
    f.drawText(L"Plugin bridge", f.take(160, 24), TextStyle::title, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.setX(f.contentWidth() + 18 - 320);
    const std::wstring status = model_.bridgeRunning ? L"listening on 127.0.0.1:" + std::to_wstring(settings_.bridgePort) : L"off";
    f.drawText(status, f.take(260, 24), TextStyle::mono, model_.bridgeRunning ? 0x30D158 : pal.text3.rgb, model_.bridgeRunning ? 1 : pal.text3.alpha, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.gap(10);
    f.toggle("bridge/on", settings_.bridgeEnabled);
    f.endRow();
    f.space(4);
    f.label(L"App plugins (Photoshop UXP, VS Code, scripts) can request a pulse by name over a loopback HTTP call; the plugin decides when, iXpat plays it under the “App plugin pulse” rule. Sample plugin and curl example in the repo's plugins folder.", TextStyle::detail);
    f.space(6);
    f.beginRow(28);
    f.drawText(L"Token", f.take(44, 28), TextStyle::detail, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.drawText(W(settings_.bridgeToken), f.take(222, 28), TextStyle::mono, pal.text, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    f.gap(8);
    const bool copied = GetTickCount64() - copiedAt_ < 1500;
    if (f.button("bridge/copy", copied ? L"Copied" : L"Copy") && actions_.copyText) { actions_.copyText(settings_.bridgeToken); copiedAt_ = GetTickCount64(); }
    f.gap(8);
    if (f.button("bridge/new", L"New token") && actions_.newToken) actions_.newToken();
    f.setX(f.contentWidth() + 18 - 176);
    f.drawText(L"Port", f.take(36, 28), TextStyle::detail, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    int port = settings_.bridgePort;
    if (f.stepper("bridge/port", port, 1024, 65535, 1, std::to_wstring(port), 136)) settings_.bridgePort = port;
    f.endRow();
}

void SettingsWindow::diagnosticsPane(Frame& f) {
    const Palette& pal = f.pal();
    f.label(L"Input", TextStyle::title);
    f.space(4);
    auto line = [&](const wchar_t* k, const std::wstring& v, bool ok = true) {
        f.beginRow(20);
        f.drawText(k, f.take(150, 20), TextStyle::detail, pal.text2.rgb, pal.text2.alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        f.drawText(v, f.take(f.contentWidth() - 150, 20), TextStyle::mono, ok ? pal.text : 0xFF9F0A, 1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        f.endRow();
    };
    const bool hooks = model_.input.mouseHook && model_.input.keyboardHook;
    line(L"Low-level hooks", hooks ? L"installed (mouse + keyboard)" : L"NOT installed: nothing is shaped or hidden", hooks);
    line(L"Raw Input", model_.input.rawRegistered ? L"registered (mouse, keyboard, consumer)" : L"NOT registered", model_.input.rawRegistered);
    line(L"Hook calls", std::to_wstring(model_.input.mouseHookCalls) + L" mouse, " + std::to_wstring(model_.input.keyboardHookCalls) + L" keyboard");
    line(L"Raw reports", std::to_wstring(model_.input.rawMouse) + L" mouse, " + std::to_wstring(model_.input.rawKeyboard) + L" keyboard");
    line(L"UI Automation", std::to_wstring(model_.uia.polls) + L" polls: " + std::to_wstring(model_.uia.menu) + L" menu, " +
                               std::to_wstring(model_.uia.control) + L" control, " + std::to_wstring(model_.uia.drop) + L" drop, " +
                               std::to_wstring(model_.uia.word + model_.uia.line) + L" text, " + std::to_wstring(model_.uia.slider) + L" slider, " +
                               std::to_wstring(model_.uia.selection) + L" selection, " + std::to_wstring(model_.uia.scrollEnd) + L" scroll end");
    f.label(L"Windows does not hand a normal process the input aimed at elevated windows (UAC prompts, admin tools): there the mouse works, but nothing is shaped.", TextStyle::footnote);
    f.divider();
    bridgeSection(f);
    f.divider();
    f.label(L"Device", TextStyle::title);
    f.space(4);
    line(L"State", model_.device.connected ? L"connected" : L"not found (paired but asleep, or on another host)", model_.device.connected);
    line(L"Vendor / Product", L"0x045E / 0x0880");
    line(L"Firmware", W(model_.device.firmware));
    line(L"Battery", model_.device.battery ? std::to_wstring(*model_.device.battery) + L"%" : L"unknown");
    line(L"Waveform table", model_.device.tableFromDevice ? L"read from the device (feature 0x42)" : L"measured defaults (fw 5.29)");
    line(L"Last error", model_.device.lastError.empty() ? L"none" : W(model_.device.lastError), model_.device.lastError.empty());
    f.space(4);
    f.beginRow(30);
    if (f.button("diag/table", L"Read haptic table (0x42)", false, model_.device.connected) && actions_.readTable) actions_.readTable();
    f.gap(8);
    if (f.button("diag/pulse", L"Test pulse", false, model_.device.connected) && actions_.testPulse) actions_.testPulse();
    f.gap(8);
    if (f.button("diag/copy", L"Copy report") && actions_.copyText) {
        std::string s = "iXpat " IXPAT_VERSION_STRING " diagnostics\n";
        s += "connected=" + std::string(model_.device.connected ? "1" : "0") + " fw=" + model_.device.firmware + " hooks=" + (hooks ? "1" : "0") + " raw=" + (model_.input.rawRegistered ? "1" : "0") + "\n";
        s += "waveforms: ";
        for (const auto& w : model_.device.table) s += std::to_string(w.ordinal) + "=" + w.name() + "/" + std::to_string(w.durationMs) + "ms, ";
        s += "\n";
        for (const auto& l : model_.reportLog) s += l + "\n";
        actions_.copyText(s);
    }
    f.gap(8);
    if (f.button("diag/about", L"About iXpat")) tab_ = kAboutTab;
    f.endRow();
    f.divider();
    f.label(L"Raw input reports (buttons, keys; motion is not logged)", TextStyle::title);
    f.space(6);
    const Rect box = f.take(0, 180);
    f.fillRound(box, 8, pal.field.rgb, pal.field.alpha);
    float y = box.y + 6;
    for (auto it = model_.reportLog.rbegin(); it != model_.reportLog.rend() && y < box.bottom() - 14; ++it, y += 15) {
        f.drawText(W(*it), {box.x + 8, y, box.w - 16, 15}, TextStyle::monoSmall, pal.text, 0.9f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    }
}

}  // namespace ixpat::ui
