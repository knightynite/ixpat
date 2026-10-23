#include "win/uia_watch.h"

#include <objbase.h>
#include <ole2.h>
#include <uiautomation.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "core/text_count.h"
#include "win/log.h"

namespace ixpat::win {
namespace {

constexpr DWORD kPollMs = 50;
constexpr Seconds kPointerActive = 0.5;  // poll only this long after the pointer last moved
constexpr Seconds kControlSpacing = 0.06, kDropSpacing = 0.08, kWordSpacing = 0.09, kLineSpacing = 0.05;
constexpr Seconds kSliderSpacing = 0.04, kSelectionSpacing = 0.10, kScrollEndSpacing = 0.15;

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** put() { if (p) p->Release(); p = nullptr; return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

bool isControlType(CONTROLTYPEID t) {
    switch (t) {
        case UIA_ButtonControlTypeId: case UIA_CheckBoxControlTypeId: case UIA_ComboBoxControlTypeId:
        case UIA_HyperlinkControlTypeId: case UIA_RadioButtonControlTypeId: case UIA_TabItemControlTypeId:
        case UIA_SliderControlTypeId: case UIA_SplitButtonControlTypeId: case UIA_SpinnerControlTypeId:
        case UIA_ListItemControlTypeId: case UIA_TreeItemControlTypeId: case UIA_MenuBarControlTypeId:
            return true;
        default:
            return false;
    }
}

bool isDropType(CONTROLTYPEID t) {
    switch (t) {
        case UIA_ListItemControlTypeId: case UIA_ListControlTypeId: case UIA_TreeItemControlTypeId: case UIA_TreeControlTypeId:
        case UIA_EditControlTypeId: case UIA_DocumentControlTypeId: case UIA_PaneControlTypeId: case UIA_GroupControlTypeId:
        case UIA_TableControlTypeId: case UIA_DataItemControlTypeId: case UIA_ButtonControlTypeId: case UIA_TabItemControlTypeId:
        case UIA_WindowControlTypeId: case UIA_ImageControlTypeId: case UIA_DataGridControlTypeId:
            return true;
        default:
            return false;
    }
}

bool isSelectableType(CONTROLTYPEID t) {
    return t == UIA_TabItemControlTypeId || t == UIA_ListItemControlTypeId || t == UIA_TreeItemControlTypeId ||
           t == UIA_DataItemControlTypeId || t == UIA_RadioButtonControlTypeId;
}

}  // namespace

struct UiaWatch::Impl {
    Com<IUIAutomation> uia;
    Com<IUIAutomationCacheRequest> cache;
    Com<IUIAutomationTreeWalker> walker;
    DWORD ownPid = GetCurrentProcessId();
    // Identity of what was under the pointer last time, per detector.
    std::string menuKey, controlKey, dropKey, selectionKey;
    bool dropFirst = true;
    int lastWords = -1, lastLines = 0;
    std::map<std::string, int> sliderBuckets;
    std::map<std::string, bool> atEnd;
    Seconds lastControl = 0, lastDrop = 0, lastSel = 0, lastSlider = 0, lastSelection = 0, lastScrollEnd = 0;
    bool wasLeftDown = false;

    bool init() {
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(uia.put())))) {
            if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(uia.put())))) return false;
        }
        if (FAILED(uia->CreateCacheRequest(cache.put()))) return false;
        cache->AddProperty(UIA_ControlTypePropertyId);
        cache->AddProperty(UIA_BoundingRectanglePropertyId);
        cache->AddProperty(UIA_IsEnabledPropertyId);
        cache->AddProperty(UIA_RuntimeIdPropertyId);
        cache->AddProperty(UIA_AutomationIdPropertyId);
        cache->AddPattern(UIA_SelectionItemPatternId);
        cache->AddPattern(UIA_RangeValuePatternId);
        cache->AddPattern(UIA_ScrollPatternId);
        cache->put_TreeScope(TreeScope_Element);
        uia->get_ControlViewWalker(walker.put());
        return true;
    }

    /// Identity that survives empty names: control type, runtime id and position.
    std::string keyOf(IUIAutomationElement* el, CONTROLTYPEID type) {
        std::string k = std::to_string(type) + "|";
        SAFEARRAY* ids = nullptr;
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(el->GetCachedPropertyValue(UIA_RuntimeIdPropertyId, &v)) && v.vt == (VT_ARRAY | VT_I4)) {
            ids = v.parray;
            LONG lo = 0, hi = -1;
            SafeArrayGetLBound(ids, 1, &lo);
            SafeArrayGetUBound(ids, 1, &hi);
            for (LONG i = lo; i <= hi; ++i) {
                int n = 0;
                SafeArrayGetElement(ids, &i, &n);
                k += std::to_string(n) + ",";
            }
        }
        VariantClear(&v);
        RECT r{};
        if (SUCCEEDED(el->get_CachedBoundingRectangle(&r))) k += "|" + std::to_string(r.left) + "," + std::to_string(r.top);
        return k;
    }

    Com<IUIAutomationElement> elementUnder(POINT pt) {
        Com<IUIAutomationElement> el;
        HWND under = WindowFromPoint(pt);
        DWORD pid = 0;
        if (under) GetWindowThreadProcessId(under, &pid);
        if (!under || pid == ownPid) return el;  // never hit-test our own windows (or nothing)
        uia->ElementFromPointBuildCache(pt, cache.p, el.put());
        return el;
    }
};

UiaWatch::UiaWatch() = default;
UiaWatch::~UiaWatch() { stop(); }

void UiaWatch::setConfig(const UiaConfig& c) {
    AcquireSRWLockExclusive(&lock_);
    cfg_ = c;
    ReleaseSRWLockExclusive(&lock_);
}

UiaStats UiaWatch::stats() const {
    AcquireSRWLockShared(&lock_);
    UiaStats s = stats_;
    ReleaseSRWLockShared(&lock_);
    return s;
}

void UiaWatch::noteClick() {
    clickAtMs_ = static_cast<std::int64_t>(GetTickCount64());
    clickPending_ = true;
}
void UiaWatch::noteWheel() {
    wheelAtMs_ = static_cast<std::int64_t>(GetTickCount64());
    wheelPending_ = true;
}

void UiaWatch::start(HWND hwnd, UINT message) {
    if (thread_.joinable()) return;
    hwnd_ = hwnd;
    message_ = message;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { loop(); });
}

void UiaWatch::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void UiaWatch::loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Impl impl;
    impl_ = &impl;
    if (!impl.init()) {
        debugLog("uia: CUIAutomation unavailable; menu, control and selection ticks are off");
        CoUninitialize();
        impl_ = nullptr;
        return;
    }
    while (WaitForSingleObject(stopEvent_, kPollMs) == WAIT_TIMEOUT) tick();
    impl_ = nullptr;
    CoUninitialize();
}

void UiaWatch::tick() {
    UiaConfig cfg;
    AcquireSRWLockShared(&lock_);
    cfg = cfg_;
    ReleaseSRWLockShared(&lock_);
    if (!cfg.any() || !now || !lastPointerMove) return;
    Impl& im = *impl_;
    const Seconds t = now();
    const bool moved = t - lastPointerMove() < kPointerActive;
    const bool down = leftButtonDown ? leftButtonDown() : false;
    const std::int64_t nowMs = static_cast<std::int64_t>(GetTickCount64());
    const bool clickCheck = clickPending_ && nowMs - clickAtMs_ >= 80;
    const bool wheelCheck = wheelPending_ && nowMs - wheelAtMs_ >= 100;
    if (!moved && !down && !clickCheck && !wheelCheck) {
        im.menuKey.clear();
        im.controlKey.clear();
        return;
    }
    auto post = [&](HapticEvent e) { PostMessageW(hwnd_, message_, static_cast<WPARAM>(e), 0); };
    auto bump = [&](std::uint32_t UiaStats::*field, const char* name) {
        AcquireSRWLockExclusive(&lock_);
        const std::uint32_t n = ++(stats_.*field);
        ReleaseSRWLockExclusive(&lock_);
        if (n == 1) debugLog(std::string("uia: first ") + name + " tick");
    };
    AcquireSRWLockExclusive(&lock_);
    ++stats_.polls;
    ReleaseSRWLockExclusive(&lock_);

    POINT pt;
    GetCursorPos(&pt);
    Com<IUIAutomationElement> el = im.elementUnder(pt);
    CONTROLTYPEID type = 0;
    BOOL enabled = TRUE;
    std::string key;
    if (el) {
        el->get_CachedControlType(&type);
        el->get_CachedIsEnabled(&enabled);
        key = im.keyOf(el.p, type);
    }

    // Menus: the highlighted item changed while sweeping (ElementFromPoint reaches context menus too).
    if (cfg.menuTicks && moved) {
        const std::string k = el && type == UIA_MenuItemControlTypeId ? key : std::string();
        if (k != im.menuKey) {
            const bool wasNone = im.menuKey.empty();
            im.menuKey = k;
            if (!k.empty() && !(wasNone && !down)) {  // the first item after none: arriving, not sweeping (Mac: menu bar click)
                post(HapticEvent::menuHighlight);
                bump(&UiaStats::menu, "menu");
            } else if (!k.empty()) {
                post(HapticEvent::menuHighlight);
                bump(&UiaStats::menu, "menu");
            }
        }
    }
    // Controls: one tick when the pointer arrives on a button, tab, link, pop-up.
    if (cfg.controlTicks && moved && !down) {
        const std::string k = el && isControlType(type) && enabled ? key : std::string();
        if (k != im.controlKey) {
            im.controlKey = k;
            if (!k.empty() && t - im.lastControl >= kControlSpacing) {
                im.lastControl = t;
                post(HapticEvent::controlHover);
                bump(&UiaStats::control, "control");
            }
        }
    } else if (!moved) {
        im.controlKey.clear();
    }
    // Drop targets: while dragging, the element under the pointer changes (the first one is where the drag began).
    if (cfg.dropTicks && down) {
        const std::string k = el && isDropType(type) ? key : std::string();
        if (!im.wasLeftDown) { im.dropKey = k; im.dropFirst = true; }
        else if (k != im.dropKey) {
            im.dropKey = k;
            if (!k.empty() && !im.dropFirst && t - im.lastDrop >= kDropSpacing) {
                im.lastDrop = t;
                post(HapticEvent::dropTarget);
                bump(&UiaStats::drop, "drop");
            }
            im.dropFirst = false;
        }
    }
    // Text selection while the left button is down: words and lines of the focused element's selection.
    if (cfg.textTicks && down) {
        Com<IUIAutomationElement> focused;
        if (SUCCEEDED(im.uia->GetFocusedElement(focused.put())) && focused) {
            Com<IUIAutomationTextPattern> text;
            if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, reinterpret_cast<void**>(text.put()))) && text) {
                Com<IUIAutomationTextRangeArray> ranges;
                if (SUCCEEDED(text->GetSelection(ranges.put())) && ranges) {
                    int count = 0;
                    ranges->get_Length(&count);
                    if (count > 0) {
                        Com<IUIAutomationTextRange> range;
                        if (SUCCEEDED(ranges->GetElement(0, range.put())) && range) {
                            BSTR s = nullptr;
                            if (SUCCEEDED(range->GetText(4096, &s)) && s) {
                                const TextCount tc = countWordsLines(std::wstring_view(s, SysStringLen(s)));
                                const int words = tc.words, lines = tc.lines;
                                SysFreeString(s);
                                if (im.lastWords < 0) { im.lastWords = words; im.lastLines = lines; }
                                else if (words != im.lastWords || lines != im.lastLines) {
                                    const bool crossedLine = lines != im.lastLines;
                                    im.lastWords = words;
                                    im.lastLines = lines;
                                    const Seconds spacing = crossedLine ? kLineSpacing : kWordSpacing;
                                    if (t - im.lastSel >= spacing) {
                                        im.lastSel = t;
                                        post(crossedLine ? HapticEvent::textLine : HapticEvent::textSelect);
                                        if (crossedLine) bump(&UiaStats::line, "line"); else bump(&UiaStats::word, "word");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        im.lastWords = -1;
    }
    // Sliders: 20 detents across the range while dragging the slider under the pointer.
    if (cfg.sliderTicks && down && el && type == UIA_SliderControlTypeId) {
        Com<IUIAutomationRangeValuePattern> rv;
        if (SUCCEEDED(el->GetCachedPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, reinterpret_cast<void**>(rv.put()))) && rv) {
            double v = 0, lo = 0, hi = 1;
            rv->get_CurrentValue(&v);
            rv->get_CurrentMinimum(&lo);
            rv->get_CurrentMaximum(&hi);
            if (hi > lo && std::isfinite(v)) {
                const int bucket = static_cast<int>(std::floor(std::clamp((v - lo) / (hi - lo), 0.0, 1.0) * 20));
                if (im.sliderBuckets.size() > 512) im.sliderBuckets.clear();
                auto it = im.sliderBuckets.find(key);
                if (it != im.sliderBuckets.end() && it->second != bucket && t - im.lastSlider >= kSliderSpacing) {
                    im.lastSlider = t;
                    post(HapticEvent::sliderTick);
                    bump(&UiaStats::slider, "slider");
                }
                im.sliderBuckets[key] = bucket;
            }
        }
    }
    // Selection: after a click, a tab / list item / tree item under the pointer is selected and is not the one before.
    if (clickCheck) {
        clickPending_ = false;
        if (cfg.selectionTicks && el && isSelectableType(type)) {
            Com<IUIAutomationSelectionItemPattern> sel;
            if (SUCCEEDED(el->GetCachedPatternAs(UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern, reinterpret_cast<void**>(sel.put()))) && sel) {
                BOOL selected = FALSE;
                sel->get_CurrentIsSelected(&selected);
                if (selected && key != im.selectionKey && t - im.lastSelection >= kSelectionSpacing) {
                    im.lastSelection = t;
                    im.selectionKey = key;
                    post(HapticEvent::selectionChange);
                    bump(&UiaStats::selection, "selection");
                }
            }
        }
    }
    // Scroll end: after a wheel notch, the nearest scrollable ancestor sits at 0% or 100%.
    if (wheelCheck) {
        wheelPending_ = false;
        if (cfg.scrollEndTicks && el) {
            Com<IUIAutomationElement> node;
            node.p = el.p;
            el.p->AddRef();
            for (int depth = 0; depth < 12 && node; ++depth) {
                Com<IUIAutomationScrollPattern> sp;
                if (SUCCEEDED(node->GetCurrentPatternAs(UIA_ScrollPatternId, IID_IUIAutomationScrollPattern, reinterpret_cast<void**>(sp.put()))) && sp) {
                    BOOL scrollable = FALSE;
                    sp->get_CurrentVerticallyScrollable(&scrollable);
                    if (scrollable) {
                        double pct = -1;
                        sp->get_CurrentVerticalScrollPercent(&pct);
                        CONTROLTYPEID nt = 0;
                        node->get_CurrentControlType(&nt);
                        std::string nk = std::to_string(nt) + "|scroll";
                        RECT r{};
                        if (SUCCEEDED(node->get_CurrentBoundingRectangle(&r))) nk += std::to_string(r.left) + "," + std::to_string(r.top);
                        const bool end = pct >= 0 && (pct <= 0.05 || pct >= 99.95);
                        if (im.atEnd.size() > 512) im.atEnd.clear();
                        const bool was = im.atEnd.count(nk) ? im.atEnd[nk] : false;
                        if (end && !was && t - im.lastScrollEnd >= kScrollEndSpacing) {
                            im.lastScrollEnd = t;
                            post(HapticEvent::scrollEnd);
                            bump(&UiaStats::scrollEnd, "scrollEnd");
                        }
                        im.atEnd[nk] = end;
                        break;
                    }
                }
                Com<IUIAutomationElement> parent;
                if (FAILED(im.walker->GetParentElement(node.p, parent.put())) || !parent) break;
                node.p->Release();
                node.p = parent.p;
                parent.p = nullptr;
            }
        }
    }
    im.wasLeftDown = down;
}

}  // namespace ixpat::win
