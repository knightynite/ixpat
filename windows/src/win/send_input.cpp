#include "win/log.h"
#include "win/send_input.h"

#include <cmath>
#include <vector>

namespace ixpat::win {

void WheelEmitter::emit(int dyPixels, int dxPixels, double pxPerNotch) {
    if (pxPerNotch <= 0) pxPerNotch = 60;
    auto convert = [&](int px, double& carry) {
        const double units = px * (WHEEL_DELTA / pxPerNotch) + carry;
        const double whole = std::trunc(units);
        carry = units - whole;
        return static_cast<int>(whole);
    };
    const int v = convert(dyPixels, carryY_);
    const int h = convert(dxPixels, carryX_);
    if (v || h) sendWheelUnits(v, h);
}

namespace {

void sendWheelUnitsNow(int vertical, int horizontal) {
    INPUT in[2]{};
    UINT n = 0;
    if (vertical) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.mouseData = static_cast<DWORD>(vertical);
        in[n].mi.dwFlags = MOUSEEVENTF_WHEEL;
        in[n].mi.dwExtraInfo = kOwnTag;
        ++n;
    }
    if (horizontal) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.mouseData = static_cast<DWORD>(horizontal);
        in[n].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in[n].mi.dwExtraInfo = kOwnTag;
        ++n;
    }
    if (n) {
        const long long t0 = qpcTicks();
        SendInput(n, in, sizeof(INPUT));
        const double ms = msSince(t0);
        if (ms >= 20) debugLogSlow("SendInput (wheel, injector thread)", ms);
    }
}

// One thread does the injecting; the callers only add to what is pending and wake it. Keyboard batches go out
// first and in order (a mask must precede the release it masks); wheel units coalesce while the thread is busy.
struct Injector {
    static constexpr int kBatch = 4;
    struct Batch {
        INPUT in[kBatch];
        UINT n;
    };
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE wake = CONDITION_VARIABLE_INIT;
    std::vector<Batch> keys;  // FIFO
    int pendingV = 0, pendingH = 0;
    bool running = false;

    static DWORD WINAPI run(void* p) {
        auto* self = static_cast<Injector*>(p);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        for (;;) {
            Batch b{};
            bool haveBatch = false;
            int v = 0, h = 0;
            AcquireSRWLockExclusive(&self->lock);
            while (self->keys.empty() && !self->pendingV && !self->pendingH) SleepConditionVariableSRW(&self->wake, &self->lock, INFINITE, 0);
            if (!self->keys.empty()) {
                b = self->keys.front();
                self->keys.erase(self->keys.begin());
                haveBatch = true;
            } else {
                v = self->pendingV;
                h = self->pendingH;
                self->pendingV = self->pendingH = 0;
            }
            ReleaseSRWLockExclusive(&self->lock);
            if (haveBatch) {
                const long long t0 = qpcTicks();
                SendInput(b.n, b.in, sizeof(INPUT));
                const double ms = msSince(t0);
                if (ms >= 20) debugLogSlow("SendInput (keys, injector thread)", ms);
            } else {
                sendWheelUnitsNow(v, h);
            }
        }
    }

    // Starts the thread on first use; false when none is to be had (the caller then sends in place).
    bool ensureThread() {
        if (!running) {
            if (HANDLE t = CreateThread(nullptr, 64 * 1024, &run, this, 0, nullptr)) {
                CloseHandle(t);
                running = true;
            }
        }
        return running;
    }

    void queueWheel(int v, int h) {
        AcquireSRWLockExclusive(&lock);
        const bool direct = !ensureThread();
        if (!direct) {
            pendingV += v;
            pendingH += h;
        }
        ReleaseSRWLockExclusive(&lock);
        if (direct) sendWheelUnitsNow(v, h);
        else WakeConditionVariable(&wake);
    }

    void queueKeys(const Batch& b) {
        AcquireSRWLockExclusive(&lock);
        const bool direct = !ensureThread();
        if (!direct) keys.push_back(b);
        ReleaseSRWLockExclusive(&lock);
        if (direct) SendInput(b.n, const_cast<INPUT*>(b.in), sizeof(INPUT));
        else WakeConditionVariable(&wake);
    }
};

Injector& injector() {
    static Injector i;
    return i;
}

}  // namespace

void sendWheelUnits(int vertical, int horizontal) {
    if (vertical || horizontal) injector().queueWheel(vertical, horizontal);
}

void queueHookProbe() {
    Injector::Batch b{};
    b.in[0].type = INPUT_KEYBOARD;
    b.in[0].ki.wVk = 0xE8;
    b.in[0].ki.dwFlags = KEYEVENTF_KEYUP;
    b.in[0].ki.dwExtraInfo = kOwnTag;
    b.in[1].type = INPUT_MOUSE;
    b.in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    b.in[1].mi.dwExtraInfo = kOwnTag;
    b.n = 2;
    injector().queueKeys(b);
}

void queueMaskThenWinUp(WORD vk, WORD scan) {
    Injector::Batch b{};
    for (int i = 0; i < 2; ++i) {
        b.in[i].type = INPUT_KEYBOARD;
        b.in[i].ki.wVk = 0xE8;
        b.in[i].ki.dwFlags = i ? KEYEVENTF_KEYUP : 0;
        b.in[i].ki.dwExtraInfo = kOwnTag;
    }
    b.in[2].type = INPUT_KEYBOARD;
    b.in[2].ki.wVk = vk;
    b.in[2].ki.wScan = scan;
    b.in[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
    b.in[2].ki.dwExtraInfo = kOwnTag;
    b.n = 3;
    injector().queueKeys(b);
}

void sendKey(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    switch (vk) {  // extended keys need the flag or some apps see the wrong key
        case VK_LWIN: case VK_RWIN: case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT: case VK_UP:
        case VK_DOWN: case VK_SNAPSHOT: case VK_DIVIDE: case VK_NUMLOCK: case VK_MEDIA_NEXT_TRACK:
        case VK_MEDIA_PREV_TRACK: case VK_MEDIA_PLAY_PAUSE: case VK_MEDIA_STOP: case VK_VOLUME_UP:
        case VK_VOLUME_DOWN: case VK_VOLUME_MUTE: case VK_APPS:
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
        default:
            break;
    }
    in.ki.dwExtraInfo = kOwnTag;
    SendInput(1, &in, sizeof in);
}

bool sendMaskKey() {
    INPUT in[2]{};
    for (int i = 0; i < 2; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = 0xE8;
        in[i].ki.dwFlags = i ? KEYEVENTF_KEYUP : 0;
        in[i].ki.dwExtraInfo = kOwnTag;
    }
    const long long t0 = qpcTicks();
    const bool ok = SendInput(2, in, sizeof(INPUT)) == 2;
    const double ms = msSince(t0);
    if (ms >= 20) debugLogSlow("SendInput (mask key)", ms);
    return ok;
}

void sendKeyChord(std::initializer_list<WORD> vks) {
    std::vector<INPUT> in;
    in.reserve(vks.size() * 2);
    auto add = [&](WORD vk, bool down) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        i.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
            vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_INSERT || vk == VK_DELETE ||
            vk == VK_SNAPSHOT || vk == VK_APPS || vk == VK_RCONTROL || vk == VK_RMENU) {
            i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        i.ki.dwExtraInfo = kOwnTag;
        in.push_back(i);
    };
    for (WORD vk : vks) add(vk, true);
    for (auto it = std::rbegin(vks); it != std::rend(vks); ++it) add(*it, false);
    if (!in.empty()) SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}

bool sendMouseButton(DWORD downFlag, DWORD upFlag, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? downFlag : upFlag;
    in.mi.dwExtraInfo = kOwnTag;
    return SendInput(1, &in, sizeof in) == 1;
}

void sendMediaKey(WORD vk) {
    sendKey(vk, true);
    sendKey(vk, false);
}

}  // namespace ixpat::win
