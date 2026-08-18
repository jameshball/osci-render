#define Dependency_NoUpdateReadyMemo
#include "..\modules\osci_standalone\packaging\CodeDependencies.iss"

#define MyAppName "osci-render"
#define MyAppVersion "2.9.4.0"
#define MyAppPublisher "James H Ball"
#define MyAppURL "https://osci-render.com/"
#define MyAppExeName "osci-render.exe"
#define MyAppVstName "osci-render.vst3"
#define MyAppAssocName MyAppName + " Project"
#define MyAppAssocExt ".osci"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

#define MyAppId "{{3BF80B88-4241-4CAF-B7BA-267D9B34BF09}"
#define MyAppOutputBase "osci-render"
#define ProjectRoot AddBackslash(SourcePath) + "..\\"
#define MyAppStandaloneSource ProjectRoot + "Builds\\osci-render\\VisualStudio2022\\x64\\Release\\Standalone Plugin\\" + MyAppExeName
#define MyAppVstSource ProjectRoot + "Builds\\osci-render\\VisualStudio2022\\x64\\Release\\VST3\\" + MyAppVstName + "\\*"
#define MyAppVstBundleDir MyAppVstName
#define MyAppIncludeTextureInterop 1
#define TextureInteropSpoutLibrarySource ProjectRoot + "modules\\osci_texture_interop\\third_party\\spout2\\2.007.017\\windows\\x64\\SpoutLibrary.dll"
#define MyAppVstPageSubHeader "Select where the osci-render VST3 plug-in should be installed."
#define MyAppVstPageDescription "Pick the folder that will receive osci-render.vst3."

#define MyAppInstrumentVstName "osci-render-instrument.vst3"
#define MyAppInstrumentVstSource ProjectRoot + "Builds\\osci-render\\VisualStudio2022\\x64\\Release\\VST3\\" + MyAppInstrumentVstName + "\\*"

#include "..\modules\osci_standalone\packaging\CommonProductInstaller.iss"

[Files]
Source: "{#MyAppInstrumentVstSource}"; DestDir: "{code:GetVstInstallDir}\\{#MyAppInstrumentVstName}"; Flags: ignoreversion recursesubdirs createallsubdirs
