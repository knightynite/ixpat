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
#include "core/updater_parse.h"

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
    std::uint32_t stallMaxMs = 0, stallCount = 0;  // the machine standing still (why hooks get dropped)
    bool bridgeRunning = false;
    std::deque<std::string> reportLog;  // newest last, capped by the app at 150
    std::string frontApp;               // image name of the foreground app
    std::string updateStatus;           // About pane: "Checking...", "Up to date", "0.3.0 is available", an error
    std::optional<ReleaseInfo> updateAvailable;
    bool updateChecking = false;
    bool updateSelfInstall = false;  // the available update can be downloaded, verified and installed from here
    bool updateInstalling = false;
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
    std::function<void()> checkUpdates;                                              // manual check
    std::function<void()> installUpdate;                                             // download, verify, run the installer
    std::function<void()> reportIssue;                                               // the issue report: consent, bundle, GitHub
    std::function<void(const std::string&)> openUrl;
};

/// The settings window: a navigation pane with the live mouse, and a page of settings cards per section.
class SettingsWindow : public GfxWindow {
public:
    SettingsWindow(Settings& settings, SettingsModel& model, SettingsActions actions);
    ~SettingsWindow() override;
    void create(HINSTANCE inst, HICON icon);
    /// The model changed (device, engine stats, buttons): repaint.
    void refresh();
    /// A haptic was played: start a pulse ring.
    void pulse();
    /// Show the About pane (tray menu, header button).
    void showAbout();
    /// Drop any native edit / popup / pending state that points into Settings (before the model is replaced).
    void cancelEditing();
    void applyTheme();

protected:
    void build(ID2D1RenderTarget* rt, const UiInput& input) override;
    void onTimer() override;
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
    bool consumeRerun() override;
    void onSystemThemeChanged() override { applyTheme(); }

private:
    void navPane(Frame& f, float H0);
    void hapticsPane(Frame& f);
    void buttonPane(Frame& f);
    void ringPane(Frame& f);
    void appsPane(Frame& f);
    void pointerPane(Frame& f);
    void diagnosticsPane(Frame& f);
    void aboutPane(Frame& f);
    void ruleRow(Frame& f, HapticEvent e);
    void feelPresets(Frame& f);
    void actionPicker(Frame& f, const std::string& id, ButtonAction& action, float reserveRight = 0);  // reserveRight: room kept free at the row's end
    void ensureEditFont(bool mono);
    void onDpiChanged() override;
    void ringEditor(Frame& f, const std::string& id, std::vector<RingItem>& items);
    void gesturePane(Frame& f);
    void bridgeSection(Frame& f);
    void beginEdit(const Rect& r, const std::string& id, std::string* target, bool numeric);
    static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    void commitEdit();
    void cancelEdit();
    void changed();

    Settings& settings_;
    SettingsModel& model_;
    SettingsActions actions_;
    UiState st_;
    int tab_ = 0;
    std::vector<double> pulseStarts_;  // GetTickCount64 ms
    std::optional<std::string> selectedProfile_;
    std::string recordingId_;         // shortcut recorder waiting for a key
    std::optional<ButtonAction> pendingShortcut_;  // recorded, to be applied by the picker that asked
    std::string pendingTarget_;
    HWND edit_ = nullptr;
    HBRUSH editBrush_ = nullptr;      // the native edit's background, in the theme's colour
    COLORREF editBrushColor_ = 0;
    HFONT editFont_ = nullptr;        // the native edit's face, per DPI and style
    float editFontScale_ = 0;
    bool editFontMono_ = false;
    int lastTab_ = -1;
    std::string* editTarget_ = nullptr;
    std::string editId_;
    bool editNumeric_ = false;
    std::vector<std::pair<std::string, std::string>> runningApps_;
    bool runningAppsMenu_ = false;
    ULONGLONG copiedAt_ = 0;
};

}  // namespace ixpat::ui
