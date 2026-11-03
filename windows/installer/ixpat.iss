; iXpat for Windows: per-user installer (Inno Setup 6). Built by CI on win-v* tags, once per architecture:
;   ISCC.exe /DMyAppVersion=0.2.15 /DMyArch=x64 /DSourceDir=<dir with iXpat.exe> /O<out dir> windows\installer\ixpat.iss
; No elevation, no services, no drivers: files under %LOCALAPPDATA%\Programs\iXpat, settings under %LOCALAPPDATA%\iXpat.
; Both architectures share the AppId and the folder, so the ARM64 installer replaces an x64 copy in place.

#ifndef MyAppVersion
  #define MyAppVersion "0.2.15"
#endif
#ifndef MyArch
  #define MyArch "x64"
#endif
#if MyArch != "x64" && MyArch != "arm64"
  #error MyArch must be x64 or arm64
#endif
#ifndef SourceDir
  #define SourceDir "..\build\" + MyArch + "-release"
#endif
; x64compatible keeps the x64 installer usable on an ARM64 PC (it runs emulated there); arm64 is ARM64 only.
#if MyArch == "arm64"
  #define MyArchitectures "arm64"
#else
  #define MyArchitectures "x64compatible"
#endif
#define MyAppName "iXpat"
#define MyAppPublisher "AL Najafi"
#define MyAppURL "https://knightynite.github.io/ixpat/"
#define MyAppExeName "iXpat.exe"

[Setup]
TimeStampsInUTC=yes
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
OutputBaseFilename=iXpat-Setup-{#MyAppVersion}-{#MyArch}
ArchitecturesAllowed={#MyArchitectures}
ArchitecturesInstallIn64BitMode={#MyArchitectures}
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
; The app's own updater runs Setup silently with /relaunch=1 once it has quit: start the new version for it.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: WantsRelaunch

[Code]
function WantsRelaunch: Boolean;
begin
  Result := ExpandConstant('{param:relaunch|0}') = '1';
end;

[UninstallRun]
; A graceful quit (settings flushed, pointer settings restored); Restart Manager handles anything still holding files.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--quit"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "QuitApp"
