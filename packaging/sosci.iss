#define Dependency_NoUpdateReadyMemo
#include "..\modules\osci_standalone\packaging\CodeDependencies.iss"

#define MyAppName "sosci"
#define MyAppVersion "1.4.4.0"
#define MyAppPublisher "James H Ball"
#define MyAppURL "https://osci-render.com/sosci"
#define MyAppExeName "sosci.exe"
#define MyAppVstName "sosci.vst3"
#define MyAppAssocName MyAppName + " Project"
#define MyAppAssocExt ".sosci"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

#define MyAppId "{{40D20CB6-2DD1-454C-BDBB-1FB79BE5B2A2}"
#define MyAppOutputBase "sosci"
#define ProjectRoot AddBackslash(SourcePath) + "..\\"
#define MyAppStandaloneSource ProjectRoot + "Builds\\sosci\\VisualStudio2022\\x64\\Release\\Standalone Plugin\\" + MyAppExeName
#define MyAppVstSource ProjectRoot + "Builds\\sosci\\VisualStudio2022\\x64\\Release\\VST3\\" + MyAppVstName + "\\*"
#define MyAppVstBundleDir MyAppVstName
#define MyAppIncludeTextureInterop 1
#define TextureInteropSpoutLibrarySource ProjectRoot + "modules\\osci_texture_interop\\third_party\\spout2\\2.007.017\\windows\\x64\\SpoutLibrary.dll"
#define MyAppVstPageSubHeader "Select where the sosci VST3 plug-in should be installed."
#define MyAppVstPageDescription "Pick the folder that will receive sosci.vst3."

#include "..\modules\osci_standalone\packaging\CommonProductInstaller.iss"
