#include "probe.h"  // <windows.h> first: hidsdi.h needs it

#include <hidsdi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/haptic_table.h"

namespace probe {
namespace {

constexpr std::uint16_t kHapticsPage = 0x0E;
constexpr std::uint16_t kCfuPage = 0xFF0B;
constexpr std::uint8_t kHapticOutputReport = 0x43;
constexpr std::uint8_t kHapticTableReport = 0x42;

const Collection* find(const std::vector<Collection>& cols, std::uint16_t usagePage) {
    for (const auto& c : cols) {
        if (c.usagePage == usagePage) return &c;
    }
    return nullptr;
}

std::int64_t micros() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    static LARGE_INTEGER t0 = [] { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const long double elapsed = static_cast<long double>(c.QuadPart - t0.QuadPart);  // subtract first: no overflow
    return static_cast<std::int64_t>(elapsed * 1000000.0L / static_cast<long double>(f.QuadPart));
}

std::string measuredName(int ordinal) {
    for (const auto& w : ixpat::measuredWaveforms()) {
        if (w.ordinal == ordinal) return std::string(w.name());
    }
    return "?";
}

int usage() {
    std::puts("ixpat-probe: Surface Mouse (2nd Edition) hardware instrument\n"
              "  list                         HID collections of the mouse, access and report sizes\n"
              "  table                        read feature 0x42 from the haptics collection and parse it\n"
              "  play [ordinal] [intensity] [--writefile]\n"
              "                               send output 0x43 (default: all eight waveforms, intensity 3)\n"
              "  versions                     read-only CFU GET_FEATURE of the versions report (Col05)\n"
              "  log [seconds] [--inject] [--out file]\n"
              "                               Raw Input + low-level hooks, timestamped, attribution stats");
    return 2;
}

}  // namespace

int cmdList() {
    auto cols = enumerateSurfaceMouse();
    if (cols.empty()) {
        std::puts("no Surface Mouse (VID 045E PID 0880) is present: pair it with this PC and wake it");
        return 2;
    }
    for (const auto& c : cols) {
        std::printf("%s\n  usage %04X/%04X  fw %X.%02X  in %u out %u feat %u  ", narrow(c.path).c_str(), c.usagePage,
                    c.usage, c.version >> 8, c.version & 0xFF, c.inputLen, c.outputLen, c.featureLen);
        if (c.openedReadWrite) std::puts("open RW: OK");
        else std::printf("open RW: FAILED error %lu\n", c.openError);
    }
    return 0;
}

int cmdTable() {
    auto cols = enumerateSurfaceMouse();
    const auto* h = find(cols, kHapticsPage);
    if (!h) {
        std::puts("haptics collection (usage page 0x0E) not present");
        return 2;
    }
    DWORD err = 0;
    HANDLE dev = openCollection(h->path, true, err);
    if (dev == INVALID_HANDLE_VALUE) {
        std::printf("open failed: %lu\n", err);
        return 1;
    }
    if (h->featureLen < 201) {
        std::printf("feature report length is %u, expected at least 201: not the known descriptor\n", h->featureLen);
        CloseHandle(dev);
        return 1;
    }
    std::vector<std::uint8_t> buf(h->featureLen, 0);
    buf[0] = kHapticTableReport;
    const auto t0 = micros();
    const BOOL ok = HidD_GetFeature(dev, buf.data(), static_cast<ULONG>(buf.size()));
    const auto t1 = micros();
    if (!ok) {
        std::printf("HidD_GetFeature(0x42) failed: %lu\n", GetLastError());
        CloseHandle(dev);
        return 1;
    }
    std::printf("feature 0x42 (%zu bytes, %lld us):\n%s\n", buf.size(), static_cast<long long>(t1 - t0),
                hex(buf.data(), buf.size()).c_str());
    auto table = ixpat::parseHapticTable(buf);
    std::printf("%zu waveforms:\n", table.size());
    for (const auto& w : table) {
        std::printf("  ordinal %2d  usage 0x%04X  %-8s %4d ms\n", w.ordinal, w.usage, std::string(w.name()).c_str(),
                    w.durationMs);
    }
    CloseHandle(dev);
    return 0;
}

int cmdPlay(int ordinal, int intensity, bool useWriteFile) {
    auto cols = enumerateSurfaceMouse();
    const auto* h = find(cols, kHapticsPage);
    if (!h) {
        std::puts("haptics collection (usage page 0x0E) not present");
        return 2;
    }
    DWORD err = 0;
    HANDLE dev = openCollection(h->path, true, err);
    if (dev == INVALID_HANDLE_VALUE) {
        std::printf("open failed: %lu\n", err);
        return 1;
    }
    if (h->outputLen < 3) {
        std::printf("output report length is %u, expected at least 3: not the known descriptor\n", h->outputLen);
        CloseHandle(dev);
        return 1;
    }
    std::vector<int> ordinals;
    if (ordinal > 0) ordinals.push_back(ordinal);
    else
        for (int o = 3; o <= 10; ++o) ordinals.push_back(o);
    int failures = 0;
    for (int o : ordinals) {
        std::vector<std::uint8_t> buf(h->outputLen, 0);
        buf[0] = kHapticOutputReport;
        buf[1] = static_cast<std::uint8_t>(o);
        buf[2] = static_cast<std::uint8_t>(std::clamp(intensity, 0, 4));
        const auto t0 = micros();
        BOOL ok;
        DWORD written = 0;
        if (useWriteFile) ok = WriteFile(dev, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) && written == buf.size();
        else ok = HidD_SetOutputReport(dev, buf.data(), static_cast<ULONG>(buf.size()));
        const auto t1 = micros();
        const DWORD e = ok ? 0 : GetLastError();
        std::printf("ordinal %2d (%-7s) intensity %d via %s: %s (%lld us)", o, measuredName(o).c_str(), buf[2],
                    useWriteFile ? "WriteFile" : "HidD_SetOutputReport", ok ? "OK" : "FAILED",
                    static_cast<long long>(t1 - t0));
        if (ok) std::puts("");
        else std::printf(" error %lu\n", e);
        if (!ok) ++failures;
        if (ordinals.size() > 1) Sleep(350);
    }
    CloseHandle(dev);
    return failures ? 1 : 0;
}

int cmdVersions() {
    auto cols = enumerateSurfaceMouse();
    const auto* c = find(cols, kCfuPage);
    if (!c) {
        std::puts("CFU collection (usage page 0xFF0B) not present");
        return 2;
    }
    DWORD err = 0;
    HANDLE dev = openCollection(c->path, true, err);
    if (dev == INVALID_HANDLE_VALUE) {
        std::printf("open RW failed: %lu; trying access 0\n", err);
        dev = openCollection(c->path, false, err);
        if (dev == INVALID_HANDLE_VALUE) {
            std::printf("open failed: %lu\n", err);
            return 1;
        }
    }
    const std::uint8_t id = featureReportIdFor(dev, kCfuPage, 0x62);
    std::printf("versions feature usage 0x62 -> report id 0x%02X (feature length %u)\n", id, c->featureLen);
    if (!id) {
        CloseHandle(dev);
        return 1;
    }
    if (c->featureLen < 2) {
        CloseHandle(dev);
        return 1;
    }
    std::vector<std::uint8_t> buf(c->featureLen, 0);
    buf[0] = id;
    if (!HidD_GetFeature(dev, buf.data(), static_cast<ULONG>(buf.size()))) {  // a read; nothing is written
        std::printf("HidD_GetFeature(0x%02X) failed: %lu\n", id, GetLastError());
        CloseHandle(dev);
        return 1;
    }
    std::printf("raw: %s\n", hex(buf.data(), buf.size()).c_str());
    // CFU GET_FWVERSION response: [id][header 4 bytes: component count, ...][per component 8 bytes:
    // version u32 LE, bank/platform/component id...].
    if (buf.size() >= 5) {
        const int count = buf[1];
        std::printf("component count byte: %d\n", count);
        for (int i = 0; i < count && 5 + 8 * i + 8 <= static_cast<int>(buf.size()); ++i) {
            const std::uint8_t* p = buf.data() + 5 + 8 * i;
            const std::uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
            std::printf("  component %d: version 0x%08X (%u.%u.%u)  props %02x %02x %02x %02x\n", i, v,
                        (v >> 24) & 0xFF, (v >> 16) & 0xFF, v & 0xFFFF, p[4], p[5], p[6], p[7]);
        }
    }
    CloseHandle(dev);
    return 0;
}

}  // namespace probe

int main(int argc, char** argv) {
    if (argc < 2) return probe::usage();
    const std::string cmd = argv[1];
    if (cmd == "list") return probe::cmdList();
    if (cmd == "table") return probe::cmdTable();
    if (cmd == "versions") return probe::cmdVersions();
    if (cmd == "play") {
        int ordinal = 0, intensity = 3;
        bool writeFile = false;
        int pos = 0;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--writefile") == 0) writeFile = true;
            else if (pos++ == 0) ordinal = std::atoi(argv[i]);
            else intensity = std::atoi(argv[i]);
        }
        return probe::cmdPlay(ordinal, intensity, writeFile);
    }
    if (cmd == "log") {
        int seconds = 10;
        bool inject = false;
        std::string out;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--inject") == 0) inject = true;
            else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
            else seconds = std::max(1, std::atoi(argv[i]));
        }
        return probe::cmdLog(seconds, inject, out);
    }
    return probe::usage();
}
