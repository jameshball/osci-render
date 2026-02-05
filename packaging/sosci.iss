#define Dependency_NoUpdateReadyMemo
#include "CodeDependencies.iss"

#define MyAppName "sosci"
#define MyAppVersion "1.2.0.11"
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
#define MyAppVstSource ProjectRoot + "Builds\\sosci\\VisualStudio2022\\x64\\Release\\VST3\\" + MyAppVstName + "\\Contents\\x86_64-win\\" + MyAppVstName
#define MyAppVstPageSubHeader "Select where the sosci VST3 plug-in should be installed."
#define MyAppVstPageDescription "Pick the folder that will receive sosci.vst3."

#include "CommonProductInstaller.iss"