#define Dependency_NoUpdateReadyMemo
#include "CodeDependencies.iss"

#define MyAppName "osci-render"
#define MyAppVersion "2.7.2.3"
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
#define MyAppVstSource ProjectRoot + "Builds\\osci-render\\VisualStudio2022\\x64\\Release\\VST3\\" + MyAppVstName + "\\Contents\\x86_64-win\\" + MyAppVstName
#define MyAppVstPageSubHeader "Select where the osci-render VST3 plug-in should be installed."
#define MyAppVstPageDescription "Pick the folder that will receive osci-render.vst3."

#include "CommonProductInstaller.iss"