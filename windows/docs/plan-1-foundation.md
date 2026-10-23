# iXpat for Windows, Plan 1: Foundation (scaffold, core engine, probe)

**Goal:** A buildable `windows/` tree with the platform-free core of iXpat ported from Swift and unit-tested, plus the `ixpat-probe` instrument that measures the two open hardware questions on the real mouse.

**Architecture:** `src/core/` is plain C++20 with no Windows headers: settings model and Swift-compatible JSON, haptic rules and feel presets, the ratchet / free-spin state machine, the kinetic scroll integrator, the action-button chord decoder, the gesture accumulator, ring geometry, the bridge protocol and the waveform-table parser. Every unit takes time as a `double` seconds argument and reports results as values, so doctest drives it without timers or threads. `tools/probe/` is the only Win32 code in this plan.

**Tech Stack:** C++20, MSVC 14.44 (VS 2022 Build Tools), CMake 3.31 (bundled) + Ninja, nlohmann/json 3.12.0, doctest 2.5.3, static CRT (`/MT`), `/W4 /WX /permissive- /utf-8`.

**Spec:** `windows/docs/design.md`

**Execution method chosen by the user:** native (this session implements), with an independent read-only review of each plan's diff fed inline before the plan is called done.

## Global Constraints

- C++20, no exceptions across hook or thread boundaries; `core/` may throw only from JSON parsing, caught at the boundary.
- `core/` includes no `<windows.h>`; it compiles on any C++20 compiler.
- Warnings are errors: `/W4 /WX /permissive- /utf-8 /EHsc`; static CRT (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`).
- Vendored headers are pinned: nlohmann/json 3.12.0, doctest 2.5.3, SHA-256 recorded in `third_party/README.md`.
- Names, defaults and clamps come from `Sources/iXpat/Store/Settings.swift`; JSON layout is Swift's synthesized Codable (enum cases as single-key objects, `{}` for payload-less cases, nil optionals omitted, UUIDs uppercase).
- Nothing writes to HID collections other than usage page 0x0E output report 0x43. Col04 and Col05 are never written.
- Commit after every task with a message starting `windows:`.

## Review Focus

Inputs the spec implies but no test would otherwise pin; each gets a test in the owning task:

1. A hand-edited `settings.json` with `"masterIntensity": 99`, `"scrollGlide": 1e300`, `"forceClickMs": -5` must load with every value clamped into the UI range (Task 4).
2. A settings file written by the Mac containing the older `RawMap` array layout for `rules` (`["leftDown", {...}, "leftUp", {...}]`) must load (Task 4).
3. A wheel report of +240 (two notches in one event) must produce one ratchet decision and a kinetic kick capped at 2 notches, never a crash or a 240-notch burst (Tasks 6 and 7).
4. A chord arriving out of order (Win release before the fake left click) must still produce exactly one double-press, never two (Task 8).
5. A bridge request with `Content-Length` larger than the body received, or over 8192, must get a 400, never a hang or an over-read (Task 10).

---

### Task 1: Scaffold the build

**Files:**
- Create: `windows/CMakeLists.txt`, `windows/CMakePresets.json`, `windows/build.ps1`, `windows/.clang-format`, `windows/README.md`
- Create: `windows/third_party/README.md`, `windows/third_party/nlohmann/json.hpp`, `windows/third_party/doctest/doctest.h` (downloaded, pinned)
- Create: `windows/src/core/clock.h`, `windows/tests/main.cpp`, `windows/tests/test_smoke.cpp`
- Create: `.github/workflows/windows.yml`
- Modify: `.gitignore` (add `windows/build/`)

**Interfaces:**
- Produces: CMake targets `ixpat_core` (static lib, public include dirs `src` and `third_party`), `ixpat_tests` (doctest runner), `ixpat_probe` (console exe, added in Task 12). `build.ps1 [-Preset x64-debug|x64-release] [-Test]` configures, builds and optionally runs tests from a vcvars environment.
- Produces: `ixpat::Seconds` = `double`; `ixpat::MonotonicClock { virtual double now() const = 0; }` for Win32 code to implement later.

- [ ] **Step 1: Download the pinned headers and record hashes**

```powershell
New-Item -ItemType Directory -Force windows/third_party/nlohmann, windows/third_party/doctest | Out-Null
Invoke-WebRequest https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp -OutFile windows/third_party/nlohmann/json.hpp
Invoke-WebRequest https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h -OutFile windows/third_party/doctest/doctest.h
Get-FileHash windows/third_party/nlohmann/json.hpp, windows/third_party/doctest/doctest.h -Algorithm SHA256
```

Write the two hashes, versions and URLs into `windows/third_party/README.md`.

- [ ] **Step 2: Write CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.26)
project(iXpatWindows LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(ixpat_core STATIC ${CORE_SOURCES})
target_include_directories(ixpat_core PUBLIC src third_party)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(ixpat_tests ${TEST_SOURCES})
target_link_libraries(ixpat_tests PRIVATE ixpat_core)
enable_testing()
add_test(NAME core COMMAND ixpat_tests)
```

(Task 12 appends the probe target.)

- [ ] **Step 3: Write CMakePresets.json**

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "base", "hidden": true, "generator": "Ninja", "binaryDir": "${sourceDir}/build/${presetName}" },
    { "name": "x64-debug", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "x64-release", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "arm64-release", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "x64-debug", "configurePreset": "x64-debug" },
    { "name": "x64-release", "configurePreset": "x64-release" },
    { "name": "arm64-release", "configurePreset": "arm64-release" }
  ],
  "testPresets": [
    { "name": "x64-debug", "configurePreset": "x64-debug", "output": { "outputOnFailure": true } },
    { "name": "x64-release", "configurePreset": "x64-release", "output": { "outputOnFailure": true } }
  ]
}
```

- [ ] **Step 4: Write build.ps1**

Finds Build Tools or Community via `vswhere`, runs `vcvarsall.bat x64` (or `amd64_arm64` for the arm64 preset) inside `cmd /c` with the bundled CMake and Ninja on PATH, then `cmake --preset`, `cmake --build --preset`, and `ctest --preset` when `-Test` is given. Fails on the first non-zero exit.

- [ ] **Step 5: Write clock.h, tests/main.cpp and a smoke test**

```cpp
// src/core/clock.h
#pragma once
namespace ixpat {
using Seconds = double;
struct MonotonicClock { virtual ~MonotonicClock() = default; virtual Seconds now() const = 0; };
}
```

```cpp
// tests/main.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

```cpp
// tests/test_smoke.cpp
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
TEST_CASE("toolchain: json parses") { CHECK(nlohmann::json::parse("{\"a\":1}")["a"] == 1); }
```

- [ ] **Step 6: Build and run**

Run: `powershell -File windows/build.ps1 -Preset x64-debug -Test`
Expected: configure, build, `100% tests passed`.

- [ ] **Step 7: CI workflow**

`.github/workflows/windows.yml`: on push/PR touching `windows/**` and on tags `win-v*`; `windows-latest`; steps: checkout, `ilammy/msvc-dev-cmd@v1` with `arch: x64` then `cmake --preset x64-release && cmake --build --preset x64-release && ctest --preset x64-release`; a second job with `arch: amd64_arm64` building `arm64-release` without tests; upload `build/*/ixpat*.exe` as artifacts.

- [ ] **Step 8: Commit**

```bash
git add windows .github/workflows/windows.yml .gitignore
git commit -m "windows: scaffold CMake build, vendored headers, test runner, CI"
```

---

### Task 2: Haptic events and rules

**Files:**
- Create: `windows/src/core/events.h`, `windows/src/core/events.cpp`
- Test: `windows/tests/test_events.cpp`

**Interfaces:**
- Produces:
```cpp
namespace ixpat {
enum class HapticEvent : uint8_t { leftDown, leftUp, rightDown, rightUp, middleDown, scrollStep, screenEdge,
  dragEnd, windowSnap, appSwitch, actionButton, menuHighlight, textSelect, textLine, ringDetent, ringOpen,
  ringCommit, freeSpinOn, freeSpinOff, leftHold, rightHold, freeSpinTexture, gestureStep, gestureLimit,
  gestureSwipe, controlHover, hoverAffordance, dropTarget, sliderTick, selectionChange, scrollEnd,
  pluginPulse, forceClick, count_ };
constexpr std::size_t kHapticEventCount = static_cast<std::size_t>(HapticEvent::count_);
std::string_view rawName(HapticEvent);                       // "leftDown" (Swift rawValue)
std::optional<HapticEvent> hapticEventFromRaw(std::string_view);
std::string_view title(HapticEvent);                         // "Left click"
std::string_view detail(HapticEvent);                        // the long description
struct HapticEcho { int ordinal{}; int intensity{}; int delayMs{}; bool operator==(const HapticEcho&) const = default; };
struct HapticRule { bool enabled{}; int ordinal{}; int intensity{}; std::optional<HapticEcho> echo;
                    bool operator==(const HapticRule&) const = default; };
const HapticRule& defaultRule(HapticEvent);                  // HapticEvent.defaults
}
```

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("events: raw names round-trip and match Swift") {
  CHECK(ixpat::rawName(ixpat::HapticEvent::menuHighlight) == "menuHighlight");
  CHECK(ixpat::hapticEventFromRaw("forceClick") == ixpat::HapticEvent::forceClick);
  CHECK_FALSE(ixpat::hapticEventFromRaw("nope").has_value());
  for (std::size_t i = 0; i < ixpat::kHapticEventCount; ++i) {
    auto e = static_cast<ixpat::HapticEvent>(i);
    CHECK(ixpat::hapticEventFromRaw(ixpat::rawName(e)) == e);
    CHECK_FALSE(ixpat::title(e).empty());
    CHECK_FALSE(ixpat::detail(e).empty());
  }
}
TEST_CASE("events: defaults match Settings.swift") {
  using E = ixpat::HapticEvent;
  CHECK(ixpat::defaultRule(E::leftDown) == ixpat::HapticRule{false, 10, 2, std::nullopt});
  CHECK(ixpat::defaultRule(E::middleDown) == ixpat::HapticRule{true, 10, 3, std::nullopt});
  CHECK(ixpat::defaultRule(E::actionButton) == ixpat::HapticRule{true, 10, 4, std::nullopt});
  CHECK(ixpat::defaultRule(E::leftHold) == ixpat::HapticRule{false, 7, 2, std::nullopt});
  CHECK(ixpat::defaultRule(E::forceClick) == ixpat::HapticRule{true, 10, 4, std::nullopt});
}
```

- [ ] **Step 2: Run, expect compile failure** (`events.h` missing)
- [ ] **Step 3: Implement** with a single `constexpr` table `{raw, title, detail, defaultRule}` indexed by the enum, copied row by row from `Settings.swift` (the 33 cases and the `defaults` dictionary).
- [ ] **Step 4: Run tests, expect pass**
- [ ] **Step 5: Commit** `windows: core haptic events, titles, default rules`

---

### Task 3: Actions, ring items, profiles, and their Swift-compatible JSON

**Files:**
- Create: `windows/src/core/actions.h`, `windows/src/core/actions.cpp`, `windows/src/core/json_compat.h`, `windows/src/core/json_compat.cpp`
- Test: `windows/tests/test_actions_json.cpp`

**Interfaces:**
- Produces:
```cpp
namespace ixpat {
enum class ButtonGesture : uint8_t { press, doublePress, hold };
std::string_view rawName(ButtonGesture); std::optional<ButtonGesture> gestureFromRaw(std::string_view);
enum class ActionKind : uint8_t { none, actionRing, gestures, missionControl, spotlight, screenshot, lockScreen,
  playPause, showDesktop, volumeUp, volumeDown, mute, nextTrack, previousTrack, brightnessUp, brightnessDown,
  spaceLeft, spaceRight, launchApp, shortcut, shell };
std::string_view rawName(ActionKind); std::optional<ActionKind> actionKindFromRaw(std::string_view);
std::string_view title(ActionKind);                 // "Mission Control" (Windows wording decided in Plan 3)
struct ButtonAction {
  ActionKind kind = ActionKind::none;
  std::string path;        // launchApp
  int keyCode = -1; std::uint64_t flags = 0; std::string label;   // shortcut
  std::string command;     // shell
  bool isStepped() const;  // volume/brightness
  std::string describe() const;
  static ButtonAction blank(ActionKind);
  bool operator==(const ButtonAction&) const = default;
};
struct RingItem { std::string id; std::string title; std::string symbol; ButtonAction action; bool operator==(const RingItem&) const = default; };
std::vector<RingItem> defaultRingItems();           // RingItem.defaults, Windows keycodes: Copy = 0x43 ('C') flags 1 (Ctrl), Paste = 0x56
struct AppProfile {
  std::string id, bundleID, name;
  std::map<std::string, HapticRule> rules;          // keyed by raw event name
  std::optional<bool> hapticsEnabled, smoothScroll, freeSpin, ratchetDynamic;
  std::optional<double> ratchetLightRate;
  std::optional<int> scrollStepEvery, freeSpinTextureEvery;
  std::optional<std::vector<RingItem>> ringItems;
  std::optional<ButtonAction> gestureUp, gestureDown, gestureLeft, gestureRight;
};
std::string newUuidUpper();                          // "9F2C...-...." random v4, uppercase
// json_compat.h: nlohmann adl serializers producing Swift's layout
void to_json(nlohmann::json&, const HapticRule&); void from_json(const nlohmann::json&, HapticRule&);
void to_json(nlohmann::json&, const ButtonAction&); void from_json(const nlohmann::json&, ButtonAction&);
void to_json(nlohmann::json&, const RingItem&);     void from_json(const nlohmann::json&, RingItem&);
void to_json(nlohmann::json&, const AppProfile&);   void from_json(const nlohmann::json&, AppProfile&);
}
```

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("json: ButtonAction uses Swift's single-key enum layout") {
  using namespace ixpat; nlohmann::json j = ButtonAction::blank(ActionKind::missionControl);
  CHECK(j.dump() == R"({"missionControl":{}})");
  ButtonAction s; s.kind = ActionKind::shortcut; s.keyCode = 0x43; s.flags = 1; s.label = "Ctrl+C";
  CHECK(nlohmann::json(s).dump() == R"({"shortcut":{"flags":1,"keyCode":67,"label":"Ctrl+C"}})");
  auto back = nlohmann::json::parse(R"({"launchApp":{"path":"C:\\x.exe"}})").get<ButtonAction>();
  CHECK(back.kind == ActionKind::launchApp); CHECK(back.path == "C:\\x.exe");
  CHECK_THROWS(nlohmann::json::parse(R"({"teleport":{}})").get<ButtonAction>());
}
TEST_CASE("json: HapticRule omits echo when absent and keeps it when present") {
  ixpat::HapticRule r{true, 10, 2, std::nullopt};
  CHECK(nlohmann::json(r).dump() == R"({"enabled":true,"intensity":2,"ordinal":10})");
  r.echo = ixpat::HapticEcho{4, 1, 45};
  CHECK(nlohmann::json(r)["echo"]["delayMs"] == 45);
  CHECK(nlohmann::json(r).get<ixpat::HapticRule>() == r);
}
TEST_CASE("json: AppProfile round-trips with nil optionals omitted") {
  ixpat::AppProfile p; p.id = ixpat::newUuidUpper(); p.bundleID = "chrome.exe"; p.name = "Chrome"; p.smoothScroll = false;
  nlohmann::json j = p;
  CHECK_FALSE(j.contains("freeSpin")); CHECK(j["smoothScroll"] == false); CHECK(j["id"].get<std::string>().size() == 36);
  auto back = j.get<ixpat::AppProfile>(); CHECK(back.bundleID == "chrome.exe"); CHECK(back.smoothScroll == false); CHECK_FALSE(back.freeSpin.has_value());
}
TEST_CASE("uuid: uppercase v4 shape") {
  auto u = ixpat::newUuidUpper(); CHECK(u.size() == 36); CHECK(u[14] == '4'); CHECK(u == [&]{ auto s = u; for (auto& c : s) c = (char)toupper(c); return s; }());
}
```

- [ ] **Step 2: Run, expect compile failure**
- [ ] **Step 3: Implement.** `to_json(ButtonAction)` writes `{ rawName(kind): payload }` where payload is `{}` except `launchApp` -> `{"path"}`, `shortcut` -> `{"flags","keyCode","label"}`, `shell` -> `{"command"}`. `from_json` requires exactly one key and a known kind, else throws `nlohmann::json::other_error`. Optionals are written only when engaged (`if (p.x) j["x"] = *p.x;`) and read with `j.contains`.
- [ ] **Step 4: Run tests, expect pass**
- [ ] **Step 5: Commit** `windows: core actions, ring items, profiles with Swift-compatible JSON`

---

### Task 4: Settings model, load/save, sanitize, effective values

**Files:**
- Create: `windows/src/core/settings.h`, `windows/src/core/settings.cpp`
- Create: `windows/tests/fixtures/settings_mac_defaults.json` (hand-built from `Settings.swift`'s `Snapshot` with default values and one profile)
- Create: `windows/tests/fixtures/settings_rawmap_array.json` (rules in the `["key", {...}, ...]` layout)
- Test: `windows/tests/test_settings.cpp`

**Interfaces:**
- Produces:
```cpp
namespace ixpat {
enum class AppTheme : uint8_t { light, dark, system };
enum class HoldAction : uint8_t { none, contextMenu, middleClick, actionRing };   // windows.holdAction
struct WindowsSettings { HoldAction holdAction = HoldAction::contextMenu; bool startAtLogin = false; };
struct Settings {
  bool enabled = true, hapticsEnabled = true; int masterIntensity = 4;
  std::array<HapticRule, kHapticEventCount> rules;   // initialised from defaultRule
  int scrollStepEvery = 1;
  bool managePointer = false; double pointerSpeed = 3.0; bool noAcceleration = false; double scrollSpeed = 0.3125; bool reverseScroll = false;
  bool smoothScroll = true; double scrollPixelsPerNotch = 60, scrollGlide = 5, scrollMomentum = 4, scrollAccel = 1;
  bool freeSpin = true, ratchetDynamic = true; double ratchetLightRate = 10, freeSpinThreshold = 18, freeSpinMultiplier = 3;
  std::vector<RingItem> ringItems = defaultRingItems();
  std::array<ButtonAction, 3> actions;                 // press=actionRing, doublePress=missionControl, hold=screenshot
  ButtonAction gestureUp, gestureDown, gestureLeft, gestureRight;   // volumeUp, volumeDown, previousTrack, nextTrack
  double gestureStepPx = 24, gestureSwipePx = 90; bool gestureFreezePointer = true, gesturesOnHold = true;
  std::vector<AppProfile> profiles;
  bool bridgeEnabled = true; int bridgePort = 47337; std::string bridgeToken;
  bool autoCheckUpdates = true;
  int clickHoldMs = 300; bool forceClick = true; int forceClickMs = 350; int freeSpinTextureEvery = 2; bool swallowButton = true;
  AppTheme theme = AppTheme::system; std::uint32_t accentHex = 0x0A84FF;
  WindowsSettings windows;
  // not persisted
  std::optional<std::string> frontBundleID;
  const HapticRule& baseRule(HapticEvent e) const;
  const HapticRule& rule(HapticEvent e) const;        // front profile override else base
  HapticRule& baseRule(HapticEvent e);
  const AppProfile* profile() const;                  // by frontBundleID
  bool effHapticsEnabled() const; bool effSmoothScroll() const; bool effFreeSpin() const; bool effRatchetDynamic() const;
  double effRatchetLightRate() const; int effScrollStepEvery() const; int effFreeSpinTextureEvery() const;
  const std::vector<RingItem>& effRingItems() const;
  const ButtonAction& effGestureUp() const; /* Down, Left, Right likewise */
  const ButtonAction& action(ButtonGesture g) const; ButtonAction& action(ButtonGesture g);
};
Settings defaultSettings();
std::string newBridgeToken();                           // 24 chars [a-z0-9]
void sanitize(Settings&);                               // Settings.sanitize clamps
std::string toJson(const Settings&);                    // pretty, sorted keys, 2-space indent, "platform":"windows"
struct ParseError { std::string message; };
std::variant<Settings, ParseError> parseSettings(std::string_view json);   // defaults for missing keys, then sanitize
}
```

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("settings: defaults match Settings.swift") {
  auto s = ixpat::defaultSettings();
  CHECK(s.masterIntensity == 4); CHECK(s.scrollPixelsPerNotch == 60); CHECK(s.freeSpinThreshold == 18);
  CHECK(s.action(ixpat::ButtonGesture::press).kind == ixpat::ActionKind::actionRing);
  CHECK(s.action(ixpat::ButtonGesture::hold).kind == ixpat::ActionKind::screenshot);
  CHECK(s.ringItems.size() == 8); CHECK(s.accentHex == 0x0A84FF); CHECK(s.windows.holdAction == ixpat::HoldAction::contextMenu);
}
TEST_CASE("settings: Mac-written file loads, round-trips, and keeps a profile") {
  auto text = readFixture("settings_mac_defaults.json");
  auto r = ixpat::parseSettings(text); REQUIRE(std::holds_alternative<ixpat::Settings>(r));
  auto& s = std::get<ixpat::Settings>(r);
  CHECK(s.profiles.size() == 1); CHECK(s.profiles[0].bundleID == "com.google.Chrome");
  CHECK(s.rule(ixpat::HapticEvent::leftDown).ordinal == 10);
  auto again = ixpat::parseSettings(ixpat::toJson(s)); REQUIRE(std::holds_alternative<ixpat::Settings>(again));
  CHECK(ixpat::toJson(std::get<ixpat::Settings>(again)) == ixpat::toJson(s));
  CHECK(nlohmann::json::parse(ixpat::toJson(s))["platform"] == "windows");
}
TEST_CASE("settings: older RawMap array layout for rules loads") {
  auto r = ixpat::parseSettings(readFixture("settings_rawmap_array.json"));
  REQUIRE(std::holds_alternative<ixpat::Settings>(r));
  CHECK(std::get<ixpat::Settings>(r).baseRule(ixpat::HapticEvent::leftDown).enabled == true);
}
TEST_CASE("settings: hand-edited nonsense is clamped") {
  auto r = ixpat::parseSettings(R"({"masterIntensity":99,"scrollGlide":1e300,"forceClickMs":-5,"bridgePort":80,"rules":{"leftDown":{"enabled":true,"ordinal":999,"intensity":0}}})");
  REQUIRE(std::holds_alternative<ixpat::Settings>(r)); auto& s = std::get<ixpat::Settings>(r);
  CHECK(s.masterIntensity == 4); CHECK(s.scrollGlide == 10); CHECK(s.forceClickMs == 200); CHECK(s.bridgePort == 1024);
  CHECK(s.baseRule(ixpat::HapticEvent::leftDown).ordinal == 255); CHECK(s.baseRule(ixpat::HapticEvent::leftDown).intensity == 1);
}
TEST_CASE("settings: malformed JSON is an error, unknown keys are ignored") {
  CHECK(std::holds_alternative<ixpat::ParseError>(ixpat::parseSettings("{nope")));
  auto r = ixpat::parseSettings(R"({"enabled":false,"future":[1,2,3]})");
  REQUIRE(std::holds_alternative<ixpat::Settings>(r)); CHECK(std::get<ixpat::Settings>(r).enabled == false);
}
TEST_CASE("settings: profile overrides resolve through eff*") {
  auto s = ixpat::defaultSettings(); ixpat::AppProfile p; p.id = ixpat::newUuidUpper(); p.bundleID = "chrome.exe"; p.smoothScroll = false;
  p.rules["leftDown"] = ixpat::HapticRule{true, 3, 1, std::nullopt}; s.profiles.push_back(p);
  s.frontBundleID = "chrome.exe";
  CHECK_FALSE(s.effSmoothScroll()); CHECK(s.rule(ixpat::HapticEvent::leftDown).ordinal == 3); CHECK(s.baseRule(ixpat::HapticEvent::leftDown).ordinal == 10);
  s.frontBundleID = "other.exe"; CHECK(s.effSmoothScroll());
}
```

- [ ] **Step 2: Build the two fixtures** by hand from `Settings.swift` (`Snapshot` field list, `RawMap` layout, `ButtonAction` single-key objects, uppercase UUID, `accentHex` 689407, `theme` `"system"`). Run tests, expect failure.
- [ ] **Step 3: Implement** `parseSettings` with `nlohmann::json::parse` in a try block (exception -> `ParseError`), a `getOr(j, key, default)` helper, the `RawMap` reader (object or alternating array), `rules` merged over defaults with unknown keys dropped, then `sanitize`. `toJson` builds a `nlohmann::json` object (std::map keys sort) and `dump(2)`.
- [ ] **Step 4: Run tests, expect pass**
- [ ] **Step 5: Commit** `windows: core settings model with Swift-compatible JSON, clamps, profiles`

---

### Task 5: Feel presets

**Files:**
- Create: `windows/src/core/presets.h`, `windows/src/core/presets.cpp`
- Test: `windows/tests/test_presets.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct ClickFeel { std::string_view name, blurb; HapticRule down, up, hold; };
struct WheelFeel { std::string_view name, blurb; HapticRule step; int every; bool dynamic; double lightRate; bool freeSpin; HapticRule texture; int textureEvery; };
std::span<const ClickFeel> clickFeels();   // 10, same order as Swift
std::span<const WheelFeel> wheelFeels();   // 9
void applyClickFeel(Settings&, const ClickFeel&, bool right);
std::string_view clickFeelName(const Settings&, bool right);   // "Custom" when no match
void applyWheelFeel(Settings&, const WheelFeel&);
std::string_view wheelFeelName(const Settings&);
}
```

- [ ] **Step 1: Failing tests**: counts (10, 9); applying "Mechanical" then `clickFeelName(false) == "Mechanical"`; editing `leftUp.intensity` -> `"Custom"`; applying "Heavy" wheel feel sets `freeSpin == false` and `wheelFeelName() == "Heavy"`; the default settings report `"Firmware only"` for clicks and `"Classic ratchet"` for the wheel.
- [ ] **Step 2: Run, expect failure** - [ ] **Step 3: Implement** by copying the two Swift tables. - [ ] **Step 4: Pass** - [ ] **Step 5: Commit** `windows: core feel presets`

---

### Task 6: Kinetic scroll integrator

**Files:**
- Create: `windows/src/core/kinetic.h`, `windows/src/core/kinetic.cpp`
- Test: `windows/tests/test_kinetic.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct KineticConfig { bool enabled = false; double pxPerNotch = 60, glide = 5, momentum = 4, accel = 1, multiplier = 1; bool reverse = false; };
class Kinetic {
public:
  // Wheel event: notches on each axis (already /120), clock time. Starts or feeds the coast.
  void notch(double notchesY, double notchesX, Seconds now, const KineticConfig&);
  struct Step { int dy = 0, dx = 0; bool active = false; };
  Step tick(Seconds now);            // integrate since the last tick; active=false when the coast is done
  bool active() const;
  void stop();                       // a click or key kills the coast (velocities to 0)
  void reset();                      // stop + forget notch history
private:
  double velX_ = 0, velY_ = 0, carryX_ = 0, carryY_ = 0, tau_ = 0.05, tauTarget_ = 0.05; Seconds lastTick_ = 0;
  std::vector<Seconds> notchTimes_;
};
}
```

- [ ] **Step 1: Failing tests**

```cpp
static int runToRest(ixpat::Kinetic& k, double t0, int& dx) { int total = 0; dx = 0; for (double t = t0; t < t0 + 5; t += 1.0 / 120) { auto s = k.tick(t); total += s.dy; dx += s.dx; if (!s.active) break; } return total; }
TEST_CASE("kinetic: a lone slow notch travels pxPerNotch") {
  ixpat::Kinetic k; ixpat::KineticConfig c; c.enabled = true; c.pxPerNotch = 60;
  k.notch(1, 0, 10.0, c); int dx; int dy = runToRest(k, 10.0 + 1.0 / 120, dx);
  CHECK(dy >= 58); CHECK(dy <= 60); CHECK(dx == 0);
}
TEST_CASE("kinetic: a flick coasts further than the sum of its notches") {
  ixpat::Kinetic k; ixpat::KineticConfig c; c.enabled = true; c.momentum = 6; c.accel = 1;
  double t = 20.0; for (int i = 0; i < 12; ++i) { k.notch(1, 0, t, c); t += 0.03; }  // ~33 notches/s
  int dx; int dy = runToRest(k, t, dx); CHECK(dy > 12 * 60);
}
TEST_CASE("kinetic: reversal kills the coast and starts one plain notch") {
  ixpat::Kinetic k; ixpat::KineticConfig c; c.enabled = true;
  double t = 30.0; for (int i = 0; i < 10; ++i) { k.notch(1, 0, t, c); t += 0.03; }
  k.notch(-1, 0, t, c); int dx; int dy = runToRest(k, t + 1.0 / 120, dx); CHECK(dy <= -58); CHECK(dy >= -62);
}
TEST_CASE("kinetic: a +2 notch event is capped at 2 and never bursts") {
  ixpat::Kinetic k; ixpat::KineticConfig c; c.enabled = true;
  k.notch(240.0 / 120, 0, 40.0, c); int dx; int dy = runToRest(k, 40.0 + 1.0 / 120, dx); CHECK(dy <= 122); CHECK(dy >= 116);
}
TEST_CASE("kinetic: reverse flips sign and stop() ends the coast") {
  ixpat::Kinetic k; ixpat::KineticConfig c; c.enabled = true; c.reverse = true;
  k.notch(1, 0, 50.0, c); auto s = k.tick(50.0 + 1.0 / 120); CHECK(s.dy < 0); k.stop(); CHECK_FALSE(k.tick(50.1).active);
}
```

- [ ] **Step 2: Run, expect failure** - [ ] **Step 3: Implement** by transliterating `kineticNotch` (notch-rate window 0.25 s, `tauSlow = 0.015 + glide*0.008`, `tauFast = max(tauSlow, momentum*0.12)`, cap per axis at +-2, reversal branch, `coastRate`, `fast = clamp((rate-5)/20)`, `gain = (1 + accel*fast) * multiplier`, `vel += dir*px*gain/tau`) and `scrollTick` (dt clamped to [1/240, 0.05], stop below 40 px/s, tau eases toward target with `1 - exp(-dt/0.08)`, `s = vel*tau*(1-decay)`, carry).
- [ ] **Step 4: Pass** - [ ] **Step 5: Commit** `windows: core kinetic scroll integrator ported from EventMonitor`

---

### Task 7: Wheel feel (dynamic ratchet, free-spin, texture)

**Files:**
- Create: `windows/src/core/wheel_feel.h`, `windows/src/core/wheel_feel.cpp`
- Test: `windows/tests/test_wheel_feel.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct WheelFeelParams {
  bool enabled = true, freeSpin = true, ratchetDynamic = true;
  double freeSpinThreshold = 18, ratchetLightRate = 10; int scrollStepEvery = 1, freeSpinTextureEvery = 2;
  int scrollStepOrdinal = 6;                 // rule(.scrollStep).ordinal
  int collideOrdinal = 4, stepOrdinal = 6, hoverOrdinal = 3;   // from the device table (fallbacks shown)
};
struct WheelPulse { HapticEvent event; std::optional<int> ordinal; int boost = 0; };
class WheelFeelEngine {
public:
  // One wheel report (delta in notches, +-1 or more). Returns 0..2 pulses in order (e.g. freeSpinOn then nothing).
  std::vector<WheelPulse> notch(int delta, Seconds now, const WheelFeelParams&);
  // Call whenever time passes (a 300 ms timer): returns freeSpinOff when the wheel rested.
  std::optional<WheelPulse> idle(Seconds now, const WheelFeelParams&);
  bool freeSpinActive() const;
  void reset();
};
}
```

- [ ] **Step 1: Failing tests**: slow notches (gap 0.3 s) -> `scrollStep` with `boost 0`, first after a 0.7 s pause -> `boost 1`; 30 notches/s -> `scrollStep` with `ordinal == hoverOrdinal` when `scrollStepOrdinal == stepOrdinal`, else `stepOrdinal`, `boost -1`, and no two light ticks closer than 40 ms; reversal within 1 s -> one `scrollStep` with `collideOrdinal`, `boost 1` and the accumulator reset; `scrollStepEvery = 2` -> a pulse every second notch; 20 notches at 25/s -> `freeSpinOn` once, then only `freeSpinTexture` every 2 notches (>= 40 ms apart), then `idle(now + 0.35)` -> `freeSpinOff`; `freeSpin = false` never enters free-spin; `enabled = false` returns nothing; a `+2` delta counts as 2 toward `scrollStepEvery` and yields one pulse.
- [ ] **Step 2: Run, expect failure** - [ ] **Step 3: Implement** from `AppDelegate.device.onWheel` verbatim (notch window 0.25 s, rate = count/0.25, free-spin entry/exit with the 0.3 s rest, texture accumulator, reversal bump, `wheelAccum`, light-tick 40 ms cap).
- [ ] **Step 4: Pass** - [ ] **Step 5: Commit** `windows: core wheel feel state machine`

---

### Task 8: Action-button chord decoder

**Files:**
- Create: `windows/src/core/chord.h`, `windows/src/core/chord.cpp`
- Test: `windows/tests/test_chord.cpp`

**Interfaces:**
```cpp
namespace ixpat {
// Keys as the mouse's keyboard collection reports them through Raw Input (virtual-key codes).
constexpr int kVkLWin = 0x5B, kVkRWin = 0x5C, kVkLShift = 0xA0, kVkRShift = 0xA1, kVkShift = 0x10, kVkS = 0x53, kVkF23 = 0x86;
enum class ChordOutput : uint8_t { none, press, doublePress, holdBegin, holdEnd };
class ChordDecoder {
public:
  // A key from the MOUSE's keyboard collection (never the user's keyboard).
  ChordOutput key(int vk, bool down, Seconds now);
  // A left-button press from the mouse's own mouse collection: completes Win + click.
  ChordOutput mouseLeftDown(Seconds now);
  bool pressed() const;                      // a chord is down (schematic glow)
  // Hook-side queries (same thread or under the caller's lock):
  bool chordActive(Seconds now) const;       // keys from the mouse are in flight: swallow Win-chord keys
  bool swallowLeftClick(Seconds now) const;  // the fake click of the double-press chord is pending/recent
  // Ring commit: the press was consumed; the fake click that follows must still be hidden.
  void consumePress(Seconds now);
};
}
```

- [ ] **Step 1: Failing tests**

```cpp
using D = ixpat::ChordDecoder; using O = ixpat::ChordOutput;
TEST_CASE("chord: Win+Shift+S is a press, released cleanly") {
  D d; double t = 1.0;
  CHECK(d.key(ixpat::kVkLWin, true, t) == O::none); CHECK(d.key(ixpat::kVkLShift, true, t) == O::none);
  CHECK(d.key(ixpat::kVkS, true, t) == O::press); CHECK(d.pressed());
  CHECK(d.key(ixpat::kVkS, false, t + 0.05) == O::none); d.key(ixpat::kVkLShift, false, t + 0.05);
  CHECK(d.key(ixpat::kVkLWin, false, t + 0.06) == O::none); CHECK_FALSE(d.pressed());
  CHECK(d.chordActive(t + 0.3)); CHECK_FALSE(d.chordActive(t + 0.6));
}
TEST_CASE("chord: Win+Shift+F23 held is holdBegin, its release holdEnd") {
  D d; CHECK(d.key(ixpat::kVkLWin, true, 2.0) == O::none); d.key(ixpat::kVkLShift, true, 2.0);
  CHECK(d.key(ixpat::kVkF23, true, 2.0) == O::holdBegin);
  d.key(ixpat::kVkF23, false, 3.0); d.key(ixpat::kVkLShift, false, 3.0);
  CHECK(d.key(ixpat::kVkLWin, false, 3.0) == O::holdEnd);
}
TEST_CASE("chord: Win + fake left click is one double press, click swallowed") {
  D d; CHECK(d.key(ixpat::kVkLWin, true, 4.0) == O::none);
  CHECK(d.mouseLeftDown(4.02) == O::doublePress); CHECK(d.swallowLeftClick(4.1));
  CHECK(d.key(ixpat::kVkLWin, false, 4.1) == O::none);           // not a second one
  CHECK_FALSE(d.swallowLeftClick(4.8));
}
TEST_CASE("chord: Win released before the click still yields exactly one double press") {
  D d; d.key(ixpat::kVkLWin, true, 5.0);
  CHECK(d.key(ixpat::kVkLWin, false, 5.03) == O::doublePress);
  CHECK(d.mouseLeftDown(5.05) == O::none);                         // late click, still hidden
  CHECK(d.swallowLeftClick(5.05));
}
TEST_CASE("chord: consumed press hides the following fake click") {
  D d; d.key(ixpat::kVkLWin, true, 6.0); d.consumePress(6.0); CHECK_FALSE(d.pressed()); CHECK(d.swallowLeftClick(6.2));
}
```

- [ ] **Step 2: Run, expect failure** - [ ] **Step 3: Implement** as a small state struct `{chord: none|snip|win|copilot, clickSeen, deviceKeyUntil (now + 0.4), swallowClicksUntil (now + 0.5), winDown, shiftDown}` following `ActionButtonController.handle / chordDown / chordUp`. A chord is recognised when its key goes down with the modifiers held; `chordUp` fires when all mouse keys are up.
- [ ] **Step 4: Pass** - [ ] **Step 5: Commit** `windows: core action-button chord decoder`

---

### Task 9: Gesture accumulator, ring geometry, version compare

**Files:**
- Create: `windows/src/core/gesture.h`, `windows/src/core/gesture.cpp`, `windows/src/core/ring.h`, `windows/src/core/version.h`, `windows/src/core/version.cpp`
- Test: `windows/tests/test_gesture.cpp`, `windows/tests/test_ring_version.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct GestureConfig { ButtonAction up, down, left, right; double stepPx = 24, swipePx = 90; bool freezePointer = true; };
class GestureAccumulator {
public:
  void begin(); void end(); bool active() const; int firedCount() const;
  // Returns the actions to run for this delta (0..6), and consumes the motion when active.
  std::vector<ButtonAction> move(double dx, double dy, const GestureConfig&);
};
// ring.h
int ringSlotIndex(double angleRadians, int count);      // 0 at 12 o'clock, clockwise
constexpr double kRingDeadZone = 26.0; constexpr int kRingSize = 360;
// version.h
bool isNewerVersion(std::string_view candidate, std::string_view current);   // "0.2.0" > "0.1.1"
std::string stripTagPrefix(std::string_view tag);        // "win-v0.2.0" -> "0.2.0", "v0.1.1" -> "0.1.1"
}
```

- [ ] **Step 1: Failing tests**: before 10 px nothing fires and motion is consumed; horizontal lock after (12, 2); 24 px vertical -> one `up` action when moving up (negative dy), two steps for 50 px; at most 6 per move; `end()` releases; ring: angle `-pi/2` -> 0, `0` -> 2 of 8, `pi/2` -> 4; `isNewerVersion("0.2.0","0.1.1")`, not `("0.1.1","0.1.1")`, `("1.0","0.9.9")`; `stripTagPrefix("win-v0.2.0") == "0.2.0"`.
- [ ] **Step 2..5** implement from `GestureController.handleMove`, `RadialMenuController.slotIndex`, `Updater.isNewer`; commit `windows: core gestures, ring geometry, version compare`.

---

### Task 10: Bridge protocol

**Files:**
- Create: `windows/src/core/bridge_protocol.h`, `windows/src/core/bridge_protocol.cpp`
- Test: `windows/tests/test_bridge_protocol.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct HttpRequest { std::string method, path, body; };
enum class ParseState { incomplete, complete, bad };
// Feed the bytes received so far; `complete` fills `out`. Caps: 8192 body, 16384 total.
ParseState parseHttpRequest(std::string_view bytes, HttpRequest& out);
struct HttpResponse { int status; std::string body; };
struct BridgeHandler {
  std::string token;                                   // empty = no check
  std::function<std::string()> waveformsJson;          // "[{...}]"
  std::function<void(std::string_view name, int intensity)> onPlay;
  HttpResponse handle(const HttpRequest&) const;       // /ping, /waveforms, /haptic, else 404
};
std::string serializeResponse(const HttpResponse&);    // status line, JSON content type, CORS, Connection: close
}
```

- [ ] **Step 1: Failing tests**: `GET /ping` -> 200 `{"app":"iXpat","ok":true}`; `POST /haptic` with the right token and `"event":"align","intensity":9` -> 200 and `onPlay("align", 4)`; wrong token -> 401; bad JSON -> 400; name with a space -> 400; unknown path -> 404; partial header -> `incomplete`; `Content-Length: 99999` -> `bad`; `Content-Length` larger than what arrived -> `incomplete` until it arrives; `serializeResponse` contains `Access-Control-Allow-Origin: *` and `Connection: close` and the exact `Content-Length`.
- [ ] **Step 2..5** implement mirroring `Bridge.swift`; commit `windows: core bridge protocol`.

---

### Task 11: Waveform table

**Files:**
- Create: `windows/src/core/haptic_table.h`, `windows/src/core/haptic_table.cpp`
- Test: `windows/tests/test_haptic_table.cpp`

**Interfaces:**
```cpp
namespace ixpat {
struct Waveform { int ordinal; std::uint16_t usage; int durationMs; std::string_view name() const; };
std::span<const Waveform> measuredWaveforms();                         // fw 5.29 table, ordinals 3..10
std::vector<Waveform> parseHapticTable(std::span<const std::uint8_t> feature42);  // [id][50 x u16 ms][50 x u16 usage]
std::optional<int> ordinalFor(std::uint16_t usage, std::span<const Waveform>);
std::string_view waveformName(std::uint16_t usage);                    // "Click", or "Waveform 0x1234"
std::optional<std::uint16_t> usageForLowercaseName(std::string_view);   // "align" -> 0x1013, aliases: snap, guide, detent, tick, limit, done, fail, notify, select, press
}
```

- [ ] **Step 1: Failing tests**: a 201-byte buffer built from the measured table parses to 8 entries with ordinals 3..10 and the right durations; a short buffer yields empty; usages outside 0x1003..0x2FFF are skipped; `usageForLowercaseName("detent") == 0x1013`.
- [ ] **Step 2..5** implement from `MouseDevice.readHapticTable`, `Waveform.names`, and the alias list in `plugins/README.md`; commit `windows: core waveform table`.

---

### Task 12: ixpat-probe (hardware instrument)

**Files:**
- Create: `windows/tools/probe/main.cpp`, `windows/tools/probe/hid_enum.cpp/.h`, `windows/tools/probe/input_log.cpp/.h`
- Modify: `windows/CMakeLists.txt` (add `ixpat_probe`, link `hid setupapi cfgmgr32 user32`)

**Interfaces:**
- Produces a console tool:
  - `ixpat-probe list`: every HID interface with VID 045E PID 0880: path, usage page/usage, report lengths, `HidD_GetAttributes` version, whether `CreateFile` RW succeeds (error code otherwise).
  - `ixpat-probe table`: opens the 0x0E collection, `HidD_GetFeature` 0x42, prints the parsed table.
  - `ixpat-probe play [ordinal] [intensity] [--writefile]`: sends 0x43 via `HidD_SetOutputReport` (or `WriteFile`), reports success and elapsed microseconds; with no ordinal plays all eight.
  - `ixpat-probe log [seconds]`: registers Raw Input (mouse, keyboard, consumer, `RIDEV_INPUTSINK`) and `WH_MOUSE_LL` + `WH_KEYBOARD_LL` on one thread; prints each event with a QPC timestamp, source (`raw`/`hook`), device path tail, kind and data; in the hook, drains pending `WM_INPUT` first and reports at the end how many hook events had no preceding raw entry (the fallback rate). Hook procs are `noexcept` and only append to a preallocated ring buffer.
  - `ixpat-probe versions`: read-only `HidD_GetFeature` 0x62 on the Col05 CFU collection if it opens; prints raw bytes. Never writes.

- [ ] **Step 1: Write the probe**, build with `build.ps1 -Preset x64-release`.
- [ ] **Step 2: Run `list` and `table` without the mouse**: expect "no Surface Mouse" and exit 2 (the device is paired but asleep or on the Mac).
- [ ] **Step 3: With the mouse connected** (user action), run all five commands and save the output to `windows/docs/measurements-2026-10.md`: open result for 0x0E, table bytes, pulse success per waveform and per API, hook-before-raw rate over 60 s of mixed trackpad + Surface Mouse input.
- [ ] **Step 4: Commit** `windows: ixpat-probe hardware instrument` (and a second commit with the measurements when available).

---

### Task 13: Independent review and close-out

- [ ] **Step 1:** `git diff <base>..HEAD -- windows .github` into a prompt file with the review brief (ports must match `Sources/iXpat` behaviour; look for off-by-one in time windows, JSON incompatibilities with Swift's layout, hook-unsafe code in the probe). Run the reviewer read-only via stdin; save the report under `windows/docs/reviews/plan-1-review.md`.
- [ ] **Step 2:** Fix every confirmed finding with a test first; record declined findings with the reason in the same file.
- [ ] **Step 3:** Commit `windows: plan 1 review fixes`.
