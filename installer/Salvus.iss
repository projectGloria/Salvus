; Salvus Windows Installer Script (Inno Setup 6)
; References official Salvus.ico and Salvus.png directly from the repository root.

#define MyAppName "Salvus"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Salvus Contributors"
#define MyAppURL "https://github.com/projectGloria/Salvus"
#define MyAppExeName "Salvus.Desktop.exe"

[Setup]
AppId={{8D6E4B72-74F3-4E2D-A96E-9B7C54E1E201}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
LicenseFile=..\LICENSE
OutputDir=..\artifacts\installer
OutputBaseFilename=Salvus-Setup-{#MyAppVersion}-win-x64
SetupIconFile=..\Salvus.ico
UninstallDisplayIcon={app}\Salvus.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\artifacts\salvus-win-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\Salvus.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Salvus.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\Salvus.ico"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\Salvus.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\Salvus.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
