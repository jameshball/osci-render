#define Dependency_NoUpdateReadyMemo
#include "CodeDependencies.iss"

; osci-motion is a standalone app (no plug-ins), so unlike osci-render and
; sosci it doesn't use CommonProductInstaller.iss and its VST3 page.
#define MyAppName "osci-motion"
#define MyAppVersion "0.1.0.7"
#define MyAppPublisher "James H Ball"
#define MyAppURL "https://osci-render.com"
#define MyAppExeName "osci-motion.exe"
#define MyAppAssocName MyAppName + " Project"
#define MyAppAssocExt ".osci-motion"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt
#define ProjectRoot AddBackslash(SourcePath) + "..\\"
#define MyAppStandaloneSource ProjectRoot + "Builds\\osci-motion\\VisualStudio2022\\x64\\Release\\Standalone Plugin\\" + MyAppExeName
#define TextureInteropSpoutLibrarySource ProjectRoot + "modules\\osci_texture_interop\\third_party\\spout2\\2.007.017\\windows\\x64\\SpoutLibrary.dll"

[Setup]
AppId={{6B0E7C1D-4A52-4F3E-9C61-2E8D5B7A9F14}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
DisableProgramGroupPage=yes
OutputDir=build
OutputBaseFilename={#MyAppName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "deletefiles"; Description: "Remove any existing settings (Clean installation)"; Flags: unchecked

[Files]
Source: "{#MyAppStandaloneSource}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#TextureInteropSpoutLibrarySource}"; DestDir: "{commonappdata}\osci-render"; DestName: "SpoutLibrary.dll"; Flags: ignoreversion

[InstallDelete]
Type: files; Name: {userappdata}\{#MyAppName}\{#MyAppName}.settings; Tasks: deletefiles

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: "{#MyAppAssocExt}"; ValueData: ""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup: Boolean;
begin
  Dependency_AddVC2015To2022;
  Result := True;
end;
