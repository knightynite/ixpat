# iXpat for Windows

The Windows counterpart of iXpat: one native `iXpat.exe` (C++20, Win32, Direct2D) that gives the
Microsoft Surface Mouse (2nd Edition) its haptics, Action Ring, kinetic scrolling, action button
and per-app profiles on Windows 10 and 11. No driver, no service, no elevation, no runtime.

Status: in development. See `docs/design.md` for the design and `docs/plan-*.md` for the build plans.

## Build

Needs Visual Studio 2022 Build Tools (or any edition) with the "Desktop development with C++"
workload, which includes CMake and Ninja. Nothing else.

```powershell
cd windows
.\build.ps1 -Preset x64-release          # windows\build\x64-release\
.\build.ps1 -Test                        # x64 debug build + unit tests
.\build.ps1 -Preset arm64-release        # needs the "MSVC v143 ARM64 build tools" component
```

## Layout

    src/core      platform-free engine (settings, rules, ratchet, kinetic scroll, chords, bridge protocol); unit-tested
    src/win       Win32 glue: Raw Input, low-level hooks, HID haptics, UI Automation, tray, bridge, updater
    src/ui        Direct2D settings window, schematic, Action Ring
    src/app       wiring
    tests         doctest unit tests for src/core
    tools/probe   ixpat-probe.exe, the hardware instrument
    third_party   pinned header-only libraries (see third_party/README.md)
