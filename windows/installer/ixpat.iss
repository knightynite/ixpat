; iXpat for Windows: per-user installer (Inno Setup 6). Built by CI on win-v* tags:
;   ISCC.exe /DMyAppVersion=0.2.5 /DSourceDir=<dir with iXpat.exe> /O<out dir> windows\installer\ixpat.iss
; No elevation, no services, no drivers: files under %LOCALAPPDATA%\Programs\iXpat, settings under %LOCALAPPDATA%\iXpat.

#ifndef MyAppVersion
  #define MyAppVersion "0.2.5"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\x64-release"
#endif
#define MyAppName "iXpat"
#define MyAppPublisher "AL Najafi"
#define MyAppURL "https://knightynite.github.io/ixpat/"
#define MyAppExeName "iXpat.exe"

[Setup]
AppId={{B6C1E5F0-6D2E-4C8B-9B1F-5A2D9E7C4A11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion} for Windows
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL=https://github.com/knightynite/ixpat/issues
AppUpdatesURL=https://github.com/knightynite/ixpat/releases
DefaultDirName={localappdata}\Programs\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputBaseFilename=iXpat-Setup-{#MyAppVersion}-x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\res\ixpat.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\..\LICENSE
CloseApplications=yes
RestartApplications=no
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startatlogin"; Description: "Start {#MyAppName} when you sign in"; GroupDescription: "Startup:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\ixpat-probe.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README-windows.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startatlogin
; Set from inside the app later: still removed at uninstall
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#MyAppName}"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; A graceful quit (settings flushed, pointer settings restored); Restart Manager handles anything still holding files.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--quit"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "QuitApp"
