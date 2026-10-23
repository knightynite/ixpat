#include <doctest/doctest.h>

#include <cctype>
#include <ostream>
#include <string>

#include "core/actions.h"
#include "core/json_compat.h"

using ixpat::ActionKind;
using ixpat::ButtonAction;
using ixpat::ButtonGesture;

TEST_CASE("actions: raw names round-trip for gestures and kinds") {
    CHECK(ixpat::rawName(ButtonGesture::doublePress) == "doublePress");
    CHECK(ixpat::gestureFromRaw("hold") == ButtonGesture::hold);
    CHECK_FALSE(ixpat::gestureFromRaw("tap").has_value());
    CHECK(ixpat::rawName(ActionKind::missionControl) == "missionControl");
    CHECK(ixpat::actionKindFromRaw("spaceRight") == ActionKind::spaceRight);
    CHECK_FALSE(ixpat::actionKindFromRaw("").has_value());
    for (int i = 0; i <= static_cast<int>(ActionKind::shell); ++i) {
        auto k = static_cast<ActionKind>(i);
        CHECK(ixpat::actionKindFromRaw(ixpat::rawName(k)) == k);
        CHECK_FALSE(ixpat::title(k).empty());
    }
}

TEST_CASE("actions: stepped kinds are volume and brightness only") {
    CHECK(ButtonAction::blank(ActionKind::volumeUp).isStepped());
    CHECK(ButtonAction::blank(ActionKind::brightnessDown).isStepped());
    CHECK_FALSE(ButtonAction::blank(ActionKind::nextTrack).isStepped());
    CHECK_FALSE(ButtonAction::blank(ActionKind::shortcut).isStepped());
}

TEST_CASE("actions: describe names the payload") {
    CHECK(ButtonAction::blank(ActionKind::launchApp).describe() == "Open an app (none chosen)");
    ButtonAction a = ButtonAction::blank(ActionKind::launchApp);
    a.path = "C:\\Tools\\Notepad++\\notepad++.exe";
    CHECK(a.describe() == "Open notepad++");
    CHECK(ButtonAction::blank(ActionKind::shortcut).describe() == "Keyboard shortcut (not recorded)");
    ButtonAction s = ButtonAction::blank(ActionKind::shortcut);
    s.keyCode = 0x43;
    s.flags = 1;
    s.label = "Ctrl+C";
    CHECK(s.describe() == "Press Ctrl+C");
    CHECK(ButtonAction::blank(ActionKind::shell).describe() == "Shell command (empty)");
    CHECK(ButtonAction::blank(ActionKind::lockScreen).describe() == ixpat::title(ActionKind::lockScreen));
}

TEST_CASE("json: ButtonAction uses Swift's single-key enum layout") {
    nlohmann::json j = ButtonAction::blank(ActionKind::missionControl);
    CHECK(j.dump() == R"({"missionControl":{}})");
    ButtonAction s = ButtonAction::blank(ActionKind::shortcut);
    s.keyCode = 0x43;
    s.flags = 1;
    s.label = "Ctrl+C";
    CHECK(nlohmann::json(s).dump() == R"({"shortcut":{"flags":1,"keyCode":67,"label":"Ctrl+C"}})");
    ButtonAction sh = ButtonAction::blank(ActionKind::shell);
    sh.command = "echo hi";
    CHECK(nlohmann::json(sh).dump() == R"({"shell":{"command":"echo hi"}})");
    auto back = nlohmann::json::parse(R"({"launchApp":{"path":"C:\\x.exe"}})").get<ButtonAction>();
    CHECK(back.kind == ActionKind::launchApp);
    CHECK(back.path == "C:\\x.exe");
    auto mac = nlohmann::json::parse(R"({"shortcut":{"keyCode":8,"flags":1048576,"label":"⌘C"}})").get<ButtonAction>();
    CHECK(mac.kind == ActionKind::shortcut);
    CHECK(mac.flags == 1048576u);
    CHECK_THROWS(nlohmann::json::parse(R"({"teleport":{}})").get<ButtonAction>());
    CHECK_THROWS(nlohmann::json::parse(R"({"none":{},"shell":{}})").get<ButtonAction>());
    CHECK_THROWS(nlohmann::json::parse(R"("none")").get<ButtonAction>());
}

TEST_CASE("json: HapticRule omits echo when absent and keeps it when present") {
    ixpat::HapticRule r{true, 10, 2, std::nullopt};
    CHECK(nlohmann::json(r).dump() == R"({"enabled":true,"intensity":2,"ordinal":10})");
    r.echo = ixpat::HapticEcho{4, 1, 45};
    CHECK(nlohmann::json(r)["echo"]["delayMs"] == 45);
    CHECK(nlohmann::json(r).get<ixpat::HapticRule>() == r);
    CHECK(nlohmann::json::parse(R"({"enabled":false,"ordinal":3,"intensity":1})").get<ixpat::HapticRule>() ==
          ixpat::HapticRule{false, 3, 1, std::nullopt});
}

TEST_CASE("json: RingItem round-trips with its action") {
    auto items = ixpat::defaultRingItems();
    REQUIRE(items.size() == 8);
    CHECK(items[0].title == "Task View");
    CHECK(items[6].action.kind == ActionKind::shortcut);
    nlohmann::json j = items[6];
    CHECK(j["action"]["shortcut"]["keyCode"] == 0x43);
    CHECK(j["id"].get<std::string>().size() == 36);
    CHECK(j.get<ixpat::RingItem>() == items[6]);
}

TEST_CASE("json: AppProfile round-trips with nil optionals omitted") {
    ixpat::AppProfile p;
    p.id = ixpat::newUuidUpper();
    p.bundleID = "chrome.exe";
    p.name = "Chrome";
    p.smoothScroll = false;
    p.rules["leftDown"] = ixpat::HapticRule{true, 3, 1, std::nullopt};
    nlohmann::json j = p;
    CHECK_FALSE(j.contains("freeSpin"));
    CHECK_FALSE(j.contains("ringItems"));
    CHECK(j["smoothScroll"] == false);
    CHECK(j["rules"]["leftDown"]["ordinal"] == 3);
    auto back = j.get<ixpat::AppProfile>();
    CHECK(back.bundleID == "chrome.exe");
    CHECK(back.smoothScroll == false);
    CHECK_FALSE(back.freeSpin.has_value());
    CHECK(back.rules.at("leftDown").ordinal == 3);
    // A Mac profile with every optional set still loads.
    auto full = nlohmann::json::parse(R"({"id":"3B9E6B0A-6C8E-4B5B-9E3A-0F0F0F0F0F0F","bundleID":"com.google.Chrome","name":"Google Chrome",
        "rules":{},"hapticsEnabled":true,"smoothScroll":false,"freeSpin":true,"ratchetDynamic":false,"ratchetLightRate":12,
        "scrollStepEvery":2,"freeSpinTextureEvery":3,"ringItems":[],"gestureUp":{"volumeUp":{}}})")
                    .get<ixpat::AppProfile>();
    CHECK(full.ratchetLightRate == 12.0);
    CHECK(full.ringItems.has_value());
    CHECK(full.ringItems->empty());
    CHECK(full.gestureUp->kind == ActionKind::volumeUp);
    CHECK_FALSE(full.gestureDown.has_value());
}

TEST_CASE("uuid: uppercase v4 shape, unique") {
    auto u = ixpat::newUuidUpper();
    REQUIRE(u.size() == 36);
    CHECK(u[8] == '-');
    CHECK(u[13] == '-');
    CHECK(u[14] == '4');
    CHECK(u[18] == '-');
    CHECK(u[23] == '-');
    for (char c : u) CHECK((c == '-' || std::isxdigit(static_cast<unsigned char>(c))));
    for (char c : u) CHECK_FALSE((c >= 'a' && c <= 'f'));
    CHECK(u != ixpat::newUuidUpper());
}
