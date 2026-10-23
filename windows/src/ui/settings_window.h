#pragma once
#include <windows.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/haptic_engine.h"
#include "core/settings.h"
#include "ui/gfx_window.h"
#include "ui/im.h"
#include "ui/schematic.h"
#include "win/hid_device.h"
#include "win/input_thread.h"
#include "win/uia_watch.h"

namespace ixpat::ui {

/// Everything the window shows that is not in Settings, fed by the app.
struct SettingsModel {
    win::DeviceInfo device;
    int pulseCount = 0;
    std::string lastEvent = "-", lastPlayed = "-";
    std::string lastGesture = "-";
    bool actionPressed = false;
    std::uint8_t buttons = 0;
    int wheelTotal = 0;
    win::InputStatus input;
    win::UiaStats uia;
    bool bridgeRunning = false;
    std::deque<std::string> reportLog;  // newest last, capped by the app at 150
    std::string frontApp;               // image name of the foreground app
};

/// What the window asks the app to do.
struct SettingsActions {
    std::function<void()> changed;                                // settings edited: save + push
    std::function<void(const HapticRule&, const std::string&)> preview;
    std::function<void(const ButtonAction&, bool hold)> run;
    std::function<void()> openRing;
    std::function<void()> testPulse;
    std::function<void()> newToken;
    std::function<void()> readTable;
    std::function<void(const std::string&)> copyText;
    std::function<void()> showSetup;
    std::function<void()> showAbout;
    std::function<void()> openMouseSettings;
    std::function<std::vector<std::pair<std::string, std::string>>()> runningApps;  // (image name, title)
    std::function<std::optional<std::string>()> chooseExe;                           // file dialog
    std::function<std::optional<std::string>()> chooseApp;                           // file dialog for an app
};

/// The settings window (MainWindow.swift): header, hero + live tiles, pill tabs and six panes.
class SettingsWindow : public GfxWindow {
public:
    SettingsWindow(Settings& settings, SettingsModel& model, SettingsActions actions);
    void create(HINSTANCE inst, HICON icon);
    /// The model changed (device, engine stats, buttons): repaint.
    void refresh();
    /// A haptic was played: start a pulse ring.
    void pulse();
    void applyTheme();

protected:
    void build(ID2D1RenderTarget* rt, const UiInput& input) override;
    void onTimer() override;
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
    void header(Frame& f);
    void heroTile(Frame& f, float width, float height);
    void liveTile(Frame& f, float width, float height);
    void hapticsPane(Frame& f);
    void buttonPane(Frame& f);
    void ringPane(Frame& f);
    void appsPane(Frame& f);
    void pointerPane(Frame& f);
    void diagnosticsPane(Frame& f);
    void ruleRow(Frame& f, HapticEvent e);
    void feelPresets(Frame& f);
    void actionPicker(Frame& f, const std::string& id, ButtonAction& action);
    void ringEditor(Frame& f, const std::string& id, std::vector<RingItem>& items);
    void gesturePane(Frame& f);
    void bridgeSection(Frame& f);
    void beginEdit(const Rect& r, const std::string& id, std::string* target, bool numeric);
    void commitEdit();
    void cancelEdit();
    void changed();

    Settings& settings_;
    SettingsModel& model_;
    SettingsActions actions_;
    UiState st_;
    int tab_ = 0;
    std::vector<float> pulseStarts_;  // GetTickCount64 ms
    std::optional<std::string> selectedProfile_;
    std::string recordingId_;         // shortcut recorder waiting for a key
    std::optional<ButtonAction> pendingShortcut_;  // recorded, to be applied by the picker that asked
    std::string pendingTarget_;
    HWND edit_ = nullptr;
    std::string* editTarget_ = nullptr;
    std::string editId_;
    bool editNumeric_ = false;
    std::vector<std::pair<std::string, std::string>> runningApps_;
    bool runningAppsMenu_ = false;
    ULONGLONG copiedAt_ = 0;
};

}  // namespace ixpat::ui
