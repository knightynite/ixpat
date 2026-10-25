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
* The executables and the installer are signed (publisher: AL Najafi).
* Updates: the app asks GitHub once a day. An installed copy can download
  the new installer, check its signature against the publisher's name and
  install it when you say so; a zip copy opens the release page instead.
* Report an issue (About): writes a zip next to the logs with the log tails,
  your settings without the bridge token and, only if you choose it, the
  latest crash report; nothing is sent unless you attach it to the issue.

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
