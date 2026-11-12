; Builds Streamline-Setup.exe from the folder the workflow assembles.
; Run:  iscc /DAppVersion=0.7.0 /DDistDir=C:\path\to\dist /DOutDir=C:\path\out streamline.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef DistDir
  #define DistDir "dist"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{B3E0A7D4-5C21-4F8A-9D36-71C4E2A9F0B8}
AppName=Streamline
AppVersion={#AppVersion}
AppPublisher=whozdgt
AppPublisherURL=https://github.com/whozdgt/Streamline
; Installs for the current person only, so no administrator prompt is needed.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\Streamline
DefaultGroupName=Streamline
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=Streamline-Setup
SetupIconFile=musicplayer.ico
UninstallDisplayIcon={app}\musicplayer.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; The Intel/AMD build also runs on Windows on ARM (it is emulated there).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Put a Streamline shortcut on the desktop"; Flags: unchecked

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Streamline"; Filename: "{app}\musicplayer.exe"
Name: "{autodesktop}\Streamline"; Filename: "{app}\musicplayer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\musicplayer.exe"; Description: "Start Streamline"; Flags: nowait postinstall skipifsilent
