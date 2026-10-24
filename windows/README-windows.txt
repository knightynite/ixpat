iXpat for Windows
=================

Haptics, an Action Ring, kinetic scrolling and force click for the Microsoft
Surface Mouse (2nd Edition). This is the Windows port of the macOS app:
https://knightynite.github.io/ixpat/

What you need
-------------
* Windows 10 (1809 or later) or Windows 11, x64 or ARM64.
* A Surface Mouse (2nd Edition) paired over Bluetooth (Settings > Bluetooth).
  No driver, no Surface app, no elevation: iXpat only talks to the mouse's
  haptics interface and watches the input Windows already delivers.

Run it
------
* Zip: unzip anywhere and start iXpat.exe. It lives in the notification area
  (tray); left-click the icon for the settings window, right-click for the
  menu.
* Installer: iXpat-Setup-<version>-x64.exe installs per user under
  %LOCALAPPDATA%\Programs\iXpat with an optional "start at login" task.
* The builds are not code-signed. SmartScreen may show "Windows protected
  your PC": choose More info > Run anyway. Updates are never installed
  silently; the app only opens the release page when a newer version exists.

Where things live
-----------------
* Settings:   %LOCALAPPDATA%\iXpat\settings.json (same layout as the Mac app;
              edits are picked up live)
* Logs:       %LOCALAPPDATA%\iXpat\debug.log and reports.log
* Crash dumps: %LOCALAPPDATA%\iXpat\crash-<time>-<pid>.dmp, the last five kept.
              A dump is a copy of the app's memory at the moment it failed and
              can hold your settings and the state of recent input. It stays on
              this PC; attach one to an issue only if you are happy to share it.
* Portable:   set IXPAT_DATA_DIR to keep everything in a folder of your choice
* ixpat-probe.exe lists the mouse's HID collections, reads the waveform table
  and plays a waveform (ixpat-probe list | table | play <ordinal> <intensity>).

Limits
------
* Windows does not deliver the input aimed at elevated windows (UAC prompts,
  admin tools) to a normal process: the mouse works there, nothing is shaped.
* Firmware updates for the mouse come through Windows Update (the Surface
  "CFU" driver), not through iXpat.

Free and open source under the MIT License. Not affiliated with or endorsed by
Microsoft. Surface is a trademark of Microsoft Corporation.
Source: https://github.com/knightynite/ixpat  Issues: https://github.com/knightynite/ixpat/issues
