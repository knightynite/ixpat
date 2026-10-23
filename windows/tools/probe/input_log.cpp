// `ixpat-probe log`: Raw Input and the low-level hooks on one thread, timestamped, to measure
// whether the hook sees an event before or after its WM_INPUT and how often attribution would
// fall back to the time window.
#include "probe.h"

#include <hidusage.h>

#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <thread>

namespace probe {
namespace {

enum class Src : std::uint8_t { raw, hook };
enum class Kind : std::uint8_t { move, button, wheel, key, other };

struct Event {
    std::int64_t qpc;
    Src src;
    Kind kind;
    bool fromMouse;      // raw: the Surface Mouse; hook: attributed to it
    bool matched;        // hook only: a raw entry of the same kind was waiting
    bool matchedViaDrain;  // hook only: the raw entry arrived through the in-hook PeekMessage drain
    bool injected;       // hook: LLMHF_INJECTED / LLKHF_INJECTED; raw: hDevice == NULL
    std::int32_t a, b;   // move: dx,dy (raw) or x,y (hook); wheel: delta; button: flags; key: vk, down
    std::string device;  // raw: tail of the device name
};

constexpr std::size_t kMaxEvents = 200000;
std::vector<Event> gEvents;  // preallocated; appended only from the input thread (hooks run on it)
std::deque<Event> gRawFifo;  // raw entries not yet matched by a hook event
std::map<HANDLE, std::string> gDeviceNames;
std::map<HANDLE, bool> gDeviceIsMouse;
HWND gWnd = nullptr;
HHOOK gMouseHook = nullptr, gKeyHook = nullptr;
std::atomic<int> gHookCalls{0};
std::int64_t gQpcFreq = 1;

std::int64_t now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

void push(Event e) {
    if (gEvents.size() < kMaxEvents) gEvents.push_back(std::move(e));
}

const std::string& deviceName(HANDLE h) {
    auto it = gDeviceNames.find(h);
    if (it != gDeviceNames.end()) return it->second;
    std::string name = "(injected)";
    bool mouse = false;
    if (h) {
        UINT size = 0;
        GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &size);
        std::wstring w(size, L'\0');
        if (size && GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, w.data(), &size) != static_cast<UINT>(-1)) {
            while (!w.empty() && w.back() == L'\0') w.pop_back();
            mouse = isSurfaceMousePath(w);
            name = narrow(w.size() > 60 ? w.substr(w.size() - 60) : w);
        }
    }
    gDeviceIsMouse[h] = mouse;
    return gDeviceNames.emplace(h, std::move(name)).first->second;
}

Kind rawMouseKind(const RAWMOUSE& m) {
    if (m.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL)) return Kind::wheel;
    if (m.usButtonFlags & 0x03FF) return Kind::button;
    if (m.lLastX || m.lLastY) return Kind::move;
    return Kind::other;
}

void onRawInput(HRAWINPUT hRaw) {
    UINT size = 0;
    GetRawInputData(hRaw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (!size) return;
    std::vector<std::uint8_t> buf(size);
    if (GetRawInputData(hRaw, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
    auto* ri = reinterpret_cast<RAWINPUT*>(buf.data());
    Event e{};
    e.qpc = now();
    e.src = Src::raw;
    e.device = deviceName(ri->header.hDevice);
    e.fromMouse = gDeviceIsMouse[ri->header.hDevice];
    e.injected = ri->header.hDevice == nullptr;
    if (ri->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = ri->data.mouse;
        e.kind = rawMouseKind(m);
        if (e.kind == Kind::wheel) e.a = static_cast<SHORT>(m.usButtonData);
        else if (e.kind == Kind::button) e.a = m.usButtonFlags;
        else { e.a = m.lLastX; e.b = m.lLastY; }
    } else if (ri->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& k = ri->data.keyboard;
        e.kind = Kind::key;
        e.a = k.VKey;
        e.b = (k.Flags & RI_KEY_BREAK) ? 0 : 1;
    } else {
        e.kind = Kind::other;
    }
    if (e.kind != Kind::other) gRawFifo.push_back(e);
    push(std::move(e));
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        onRawInput(reinterpret_cast<HRAWINPUT>(lp));
        return DefWindowProcW(h, msg, wp, lp);  // required after RIDEV_INPUTSINK delivery
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/// Inside a hook callback: pull the WM_INPUT messages the system already posted for this event.
bool drainRawInput() {
    bool any = false;
    MSG m;
    while (PeekMessageW(&m, gWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {
        onRawInput(reinterpret_cast<HRAWINPUT>(m.lParam));
        DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
        any = true;
    }
    return any;
}

void attribute(Event& e) {
    // Take the oldest raw entry of the same kind. Entries of other kinds older than 100 ms are stale.
    const std::int64_t stale = gQpcFreq / 10;
    while (!gRawFifo.empty() && e.qpc - gRawFifo.front().qpc > stale) gRawFifo.pop_front();
    for (auto it = gRawFifo.begin(); it != gRawFifo.end(); ++it) {
        if (it->kind == e.kind) {
            e.matched = true;
            e.fromMouse = it->fromMouse;
            gRawFifo.erase(it);
            return;
        }
    }
    e.matched = false;
}

LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION) {
        ++gHookCalls;
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        Event e{};
        e.qpc = now();
        e.src = Src::hook;
        e.injected = (m->flags & LLMHF_INJECTED) != 0;
        switch (wp) {
            case WM_MOUSEMOVE: e.kind = Kind::move; e.a = m->pt.x; e.b = m->pt.y; break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: e.kind = Kind::wheel; e.a = static_cast<SHORT>(HIWORD(m->mouseData)); break;
            default: e.kind = Kind::button; e.a = static_cast<std::int32_t>(wp); break;
        }
        const std::size_t before = gRawFifo.size();
        bool drained = drainRawInput();
        attribute(e);
        e.matchedViaDrain = e.matched && drained && gRawFifo.size() <= before;  // the match came from the drain
        push(std::move(e));
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK keyHook(int code, WPARAM wp, LPARAM lp) noexcept {
    if (code == HC_ACTION) {
        ++gHookCalls;
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        Event e{};
        e.qpc = now();
        e.src = Src::hook;
        e.kind = Kind::key;
        e.injected = (k->flags & LLKHF_INJECTED) != 0;
        e.a = static_cast<std::int32_t>(k->vkCode);
        e.b = (k->flags & LLKHF_UP) ? 0 : 1;
        const std::size_t before = gRawFifo.size();
        bool drained = drainRawInput();
        attribute(e);
        e.matchedViaDrain = e.matched && drained && gRawFifo.size() <= before;
        push(std::move(e));
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void injectSelfTest() {
    // Runs on another thread: synthetic moves, a wheel notch and a shift tap, so the pipeline can be
    // exercised without a hand on a mouse. Injected events carry hDevice == NULL in Raw Input.
    Sleep(500);
    for (int i = 0; i < 20; ++i) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = (i % 2) ? 3 : -3;
        in.mi.dy = 1;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        in.mi.dwExtraInfo = 0x49585054;
        SendInput(1, &in, sizeof in);
        Sleep(8);
    }
    INPUT w{};
    w.type = INPUT_MOUSE;
    w.mi.mouseData = static_cast<DWORD>(WHEEL_DELTA);
    w.mi.dwFlags = MOUSEEVENTF_WHEEL;
    w.mi.dwExtraInfo = 0x49585054;
    SendInput(1, &w, sizeof w);
    Sleep(50);
    INPUT k[2]{};
    k[0].type = k[1].type = INPUT_KEYBOARD;
    k[0].ki.wVk = k[1].ki.wVk = VK_SHIFT;
    k[1].ki.dwFlags = KEYEVENTF_KEYUP;
    k[0].ki.dwExtraInfo = k[1].ki.dwExtraInfo = 0x49585054;
    SendInput(2, k, sizeof(INPUT));
}

const char* kindName(Kind k) {
    switch (k) {
        case Kind::move: return "move";
        case Kind::button: return "button";
        case Kind::wheel: return "wheel";
        case Kind::key: return "key";
        default: return "other";
    }
}

}  // namespace

int cmdLog(int seconds, bool inject, const std::string& outFile) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    gQpcFreq = f.QuadPart;
    gEvents.reserve(kMaxEvents);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ixpat-probe-input";
    RegisterClassW(&wc);
    gWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!gWnd) { std::printf("CreateWindowEx failed: %lu\n", GetLastError()); return 1; }

    RAWINPUTDEVICE rid[3]{};
    rid[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_INPUTSINK, gWnd};
    rid[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, RIDEV_INPUTSINK, gWnd};
    rid[2] = {HID_USAGE_PAGE_CONSUMER, 0x01, RIDEV_INPUTSINK, gWnd};
    if (!RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE))) {
        std::printf("RegisterRawInputDevices failed: %lu\n", GetLastError());
        return 1;
    }
    gMouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, wc.hInstance, 0);
    gKeyHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyHook, wc.hInstance, 0);
    if (!gMouseHook || !gKeyHook) { std::printf("SetWindowsHookEx failed: %lu\n", GetLastError()); return 1; }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::printf("logging for %d s%s; move the Surface Mouse, scroll, click, press the action button...\n", seconds,
                inject ? " (with injected self-test events)" : "");
    std::thread injector;
    if (inject) injector = std::thread(injectSelfTest);

    const std::int64_t start = now(), end = start + gQpcFreq * seconds;
    MSG m;
    while (now() < end) {
        DWORD waitMs = static_cast<DWORD>(std::max<std::int64_t>(1, (end - now()) * 1000 / gQpcFreq));
        MsgWaitForMultipleObjectsEx(0, nullptr, std::min<DWORD>(waitMs, 250), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (injector.joinable()) injector.join();
    UnhookWindowsHookEx(gMouseHook);
    UnhookWindowsHookEx(gKeyHook);

    // Summary
    int rawN = 0, hookN = 0, hookMatched = 0, hookDrain = 0, hookUnmatched = 0, hookMouse = 0, rawMouse = 0;
    std::map<Kind, int> unmatchedByKind;
    for (const auto& e : gEvents) {
        if (e.src == Src::raw) { ++rawN; if (e.fromMouse) ++rawMouse; continue; }
        ++hookN;
        if (e.matched) { ++hookMatched; if (e.matchedViaDrain) ++hookDrain; }
        else { ++hookUnmatched; ++unmatchedByKind[e.kind]; }
        if (e.fromMouse) ++hookMouse;
    }
    std::printf("raw events: %d (%d from the Surface Mouse)\n", rawN, rawMouse);
    std::printf("hook events: %d, matched %d (via in-hook drain %d), unmatched %d, attributed to the Surface Mouse %d\n",
                hookN, hookMatched, hookDrain, hookUnmatched, hookMouse);
    for (auto& [k, n] : unmatchedByKind) std::printf("  unmatched %s: %d\n", kindName(k), n);
    std::printf("hook callbacks: %d; devices seen:\n", gHookCalls.load());
    for (auto& [h, name] : gDeviceNames) std::printf("  %p %s %s\n", h, gDeviceIsMouse[h] ? "[SURFACE MOUSE]" : "", name.c_str());

    if (!outFile.empty()) {
        std::ofstream out(outFile, std::ios::binary);
        for (const auto& e : gEvents) {
            double ms = static_cast<double>(e.qpc - start) * 1000.0 / static_cast<double>(gQpcFreq);
            out << ms << '\t' << (e.src == Src::raw ? "raw" : "hook") << '\t' << kindName(e.kind) << '\t' << e.a << '\t'
                << e.b << '\t' << (e.fromMouse ? "surface" : "-") << '\t'
                << (e.src == Src::hook ? (e.matched ? (e.matchedViaDrain ? "drain" : "queued") : "UNMATCHED") : e.device)
                << (e.injected ? "\tinjected" : "") << '\n';
        }
        std::printf("full log: %s (%zu events)\n", outFile.c_str(), gEvents.size());
    }
    return 0;
}

}  // namespace probe
