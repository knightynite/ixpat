#include <doctest/doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

#include "core/settings.h"
#include "win/settings_store.h"

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir() {
        wchar_t buf[MAX_PATH];
        GetTempPathW(MAX_PATH, buf);
        dir = fs::path(buf) / (L"ixpat-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(GetTickCount64()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_CASE("settings store: missing file loads defaults and does not create the file by itself") {
    TempDir t;
    ixpat::win::SettingsStore store(t.dir / "settings.json");
    auto s = store.load();
    CHECK(s.masterIntensity == 4);
    CHECK(s.bridgeToken.size() == 24);
    CHECK_FALSE(fs::exists(t.dir / "settings.json"));
}

TEST_CASE("settings store: save writes atomically and load reads it back") {
    TempDir t;
    ixpat::win::SettingsStore store(t.dir / "settings.json");
    auto s = ixpat::defaultSettings();
    s.masterIntensity = 2;
    s.bridgeToken = "abcdefghijklmnopqrstuvwx";
    store.save(s);
    CHECK(fs::exists(t.dir / "settings.json"));
    CHECK_FALSE(fs::exists(t.dir / "settings.json.tmp"));
    auto back = store.load();
    CHECK(back.masterIntensity == 2);
    CHECK(back.bridgeToken == "abcdefghijklmnopqrstuvwx");
    CHECK(slurp(t.dir / "settings.json").find("\"platform\": \"windows\"") != std::string::npos);
}

TEST_CASE("settings store: a broken file is kept as settings.broken.json and defaults are used") {
    TempDir t;
    {
        std::ofstream out(t.dir / "settings.json", std::ios::binary);
        out << "{this is not json";
    }
    ixpat::win::SettingsStore store(t.dir / "settings.json");
    auto s = store.load();
    CHECK(s.masterIntensity == 4);
    CHECK(fs::exists(t.dir / "settings.broken.json"));
    CHECK(slurp(t.dir / "settings.broken.json") == "{this is not json");
    CHECK(slurp(t.dir / "settings.json") == "{this is not json");  // load never overwrites
}

TEST_CASE("settings store: debounced saves write once, 300 ms after the last change") {
    TempDir t;
    ixpat::win::SettingsStore store(t.dir / "settings.json");
    auto s = ixpat::defaultSettings();
    s.masterIntensity = 1;
    store.markDirty(s, 10.0);
    CHECK_FALSE(store.flushIfDue(10.1));
    CHECK_FALSE(fs::exists(t.dir / "settings.json"));
    s.masterIntensity = 3;
    store.markDirty(s, 10.2);
    CHECK_FALSE(store.flushIfDue(10.4));  // only 200 ms since the last change
    CHECK(store.flushIfDue(10.5));
    CHECK(store.load().masterIntensity == 3);
    CHECK_FALSE(store.flushIfDue(11.0));  // nothing pending
    s.masterIntensity = 2;
    store.markDirty(s, 12.0);
    store.flush();  // quit path: immediate
    CHECK(store.load().masterIntensity == 2);
}

TEST_CASE("settings store: the watcher reports an external edit and ignores our own save") {
    TempDir t;
    ixpat::win::SettingsStore store(t.dir / "settings.json");
    store.save(ixpat::defaultSettings());
    // A message-only window collects the notifications.
    struct Ctx {
        int notifications = 0;
    } ctx;
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_APP + 1) {
            auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            if (c) ++c->notifications;
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ixpat-test-watch";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    REQUIRE(hwnd != nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    auto pump = [&](DWORD ms) {
        const ULONGLONG end = GetTickCount64() + ms;
        MSG msg;
        while (GetTickCount64() < end) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
    };
    store.watch(hwnd, WM_APP + 1);
    pump(200);
    auto s = ixpat::defaultSettings();
    s.masterIntensity = 1;
    store.save(s);  // our own write: no notification
    pump(600);
    CHECK(ctx.notifications == 0);
    {
        std::ofstream out(t.dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << "{\"masterIntensity\": 2}";
    }
    pump(900);
    CHECK(ctx.notifications == 1);
    store.stopWatching();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
