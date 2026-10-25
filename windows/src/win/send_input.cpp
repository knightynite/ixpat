#include "win/log.h"
#include "win/send_input.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
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

// The wheel thread: units queued while it is busy add up and go out as one event.
struct WheelInjector {
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE wake = CONDITION_VARIABLE_INIT;
    int pendingV = 0, pendingH = 0;
    bool running = false;

    static DWORD WINAPI run(void* p) {
        auto* self = static_cast<WheelInjector*>(p);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        for (;;) {
            AcquireSRWLockExclusive(&self->lock);
            while (!self->pendingV && !self->pendingH) SleepConditionVariableSRW(&self->wake, &self->lock, INFINITE, 0);
            const int v = self->pendingV, h = self->pendingH;
            self->pendingV = self->pendingH = 0;
            ReleaseSRWLockExclusive(&self->lock);
            sendWheelUnitsNow(v, h);
        }
    }

    bool start() {
        if (!running) {
            if (HANDLE t = CreateThread(nullptr, 64 * 1024, &run, this, 0, nullptr)) {
                CloseHandle(t);
                running = true;
            }
        }
        return running;
    }

    void queue(int v, int h) {
        AcquireSRWLockExclusive(&lock);
        pendingV += v;
        pendingH += h;
        ReleaseSRWLockExclusive(&lock);
        WakeConditionVariable(&wake);
    }
};

// The key thread: batches in order, each with a kind, a sequence number and the time it was queued.
struct KeyInjector {
    enum class Kind : std::uint8_t { mask, release, probe };
    struct Item {
        INPUT in[4];
        UINT n;
        Kind kind;
        std::uint32_t seq;
        long long queuedQpc;
    };
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE wake = CONDITION_VARIABLE_INIT;
    std::vector<Item> items;  // FIFO, short
    bool running = false;
    std::atomic<std::uint32_t> nextSeq{0}, maskSent{0}, failures{0};
    std::atomic<long long> probeSentQpc{0};
    std::atomic<bool> probeOk{false};

    static DWORD WINAPI run(void* p) {
        auto* self = static_cast<KeyInjector*>(p);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        for (;;) {
            Item it{};
            AcquireSRWLockExclusive(&self->lock);
            while (self->items.empty()) SleepConditionVariableSRW(&self->wake, &self->lock, INFINITE, 0);
            it = self->items.front();
            self->items.erase(self->items.begin());
            ReleaseSRWLockExclusive(&self->lock);
            if (it.kind == Kind::probe && msSince(it.queuedQpc) > 1000) continue;  // stale: the activity it was for is over
            const long long t0 = qpcTicks();
            UINT sent = SendInput(it.n, it.in, sizeof(INPUT));
            for (int retry = 0; sent < it.n && it.kind != Kind::probe && retry < 3; ++retry) {  // a mask or a release must not be lost
                Sleep(5);
                sent = SendInput(it.n, it.in, sizeof(INPUT));
            }
            const double ms = msSince(t0);
            if (ms >= 20) debugLogSlow("SendInput (keys, injector thread)", ms);
            switch (it.kind) {
                case Kind::mask:
                    if (sent == it.n) self->maskSent.store(it.seq, std::memory_order_release);
                    else self->failures.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Kind::release:
                    if (sent != it.n) self->failures.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Kind::probe:
                    self->probeOk.store(sent == it.n, std::memory_order_relaxed);
                    self->probeSentQpc.store(qpcTicks(), std::memory_order_release);
                    break;
            }
        }
    }

    bool start() {
        if (!running) {
            if (HANDLE t = CreateThread(nullptr, 64 * 1024, &run, this, 0, nullptr)) {
                CloseHandle(t);
                running = true;
            }
        }
        return running;
    }

    std::uint32_t queue(Item it) {
        it.seq = nextSeq.fetch_add(1, std::memory_order_relaxed) + 1;
        it.queuedQpc = qpcTicks();
        AcquireSRWLockExclusive(&lock);
        items.push_back(it);
        ReleaseSRWLockExclusive(&lock);
        WakeConditionVariable(&wake);
        return it.seq;
    }
};

// Process-lifetime objects: the threads outlive every static destructor, so nothing is ever torn down under them.
WheelInjector& wheel() {
    static WheelInjector* w = new WheelInjector;
    return *w;
}
KeyInjector& keys() {
    static KeyInjector* k = new KeyInjector;
    return *k;
}
std::atomic<bool> gReady{false};

void maskKeys(INPUT* in) {
    for (int i = 0; i < 2; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = 0xE8;
        in[i].ki.dwFlags = i ? KEYEVENTF_KEYUP : 0;
        in[i].ki.dwExtraInfo = kOwnTag;
    }
}

}  // namespace

void startInjectors() { gReady.store(wheel().start() && keys().start(), std::memory_order_release); }
bool injectorReady() { return gReady.load(std::memory_order_acquire); }

void sendWheelUnits(int vertical, int horizontal) {
    if ((vertical || horizontal) && injectorReady()) wheel().queue(vertical, horizontal);
}

std::uint32_t queueMask() {
    if (!injectorReady()) return 0;
    KeyInjector::Item it{};
    maskKeys(it.in);
    it.n = 2;
    it.kind = KeyInjector::Kind::mask;
    return keys().queue(it);
}

std::uint32_t maskSentSequence() { return keys().maskSent.load(std::memory_order_acquire); }
std::uint32_t maskFailures() { return keys().failures.load(std::memory_order_relaxed); }

void queueWinRelease(WORD vk, WORD scan) {
    if (!injectorReady()) return;
    KeyInjector::Item it{};
    it.in[0].type = INPUT_KEYBOARD;
    it.in[0].ki.wVk = vk;
    it.in[0].ki.wScan = scan;
    it.in[0].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
    it.in[0].ki.dwExtraInfo = kOwnTag;
    it.n = 1;
    it.kind = KeyInjector::Kind::release;
    keys().queue(it);
}

void queueHookProbe() {
    if (!injectorReady()) return;
    KeyInjector::Item it{};
    it.in[0].type = INPUT_KEYBOARD;
    it.in[0].ki.wVk = 0xE8;
    it.in[0].ki.dwFlags = KEYEVENTF_KEYUP;
    it.in[0].ki.dwExtraInfo = kProbeTag;
    it.in[1].type = INPUT_MOUSE;
    it.in[1].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;  // a plain zero move never reached the hook here
    it.in[1].mi.dwExtraInfo = kProbeTag;
    it.n = 2;
    it.kind = KeyInjector::Kind::probe;
    keys().queue(it);
}

long long probeSentQpc() { return keys().probeSentQpc.load(std::memory_order_acquire); }
bool probeSendOk() { return keys().probeOk.load(std::memory_order_relaxed); }

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
