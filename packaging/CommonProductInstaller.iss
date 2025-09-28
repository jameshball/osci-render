#ifndef MyAppId
  #error "MyAppId must be defined before including CommonProductInstaller.iss"
#endif

#ifndef MyAppOutputBase
  #error "MyAppOutputBase must be defined before including CommonProductInstaller.iss"
#endif

#ifndef MyAppStandaloneSource
  #error "MyAppStandaloneSource must be defined before including CommonProductInstaller.iss"
#endif

#ifndef MyAppVstSource
  #error "MyAppVstSource must be defined before including CommonProductInstaller.iss"
#endif

#ifndef MyAppVstDefaultDir
  #define MyAppVstDefaultDir "{cf}\\VST3"
#endif

#ifndef MyAppVstPageHeader
  #define MyAppVstPageHeader "VST3 Plugin Location"
#endif

#ifndef MyAppVstPageSubHeader
  #define MyAppVstPageSubHeader "Choose the installation folder for the VST3 plugin."
#endif

#ifndef MyAppVstPageDescription
  #define MyAppVstPageDescription "Select the folder where you want to install the VST3 plugin."
#endif

#ifndef MyAppExePageHeader
  #define MyAppExePageHeader "Application Location"
#endif

#ifndef MyAppExePageSubHeader
  #define MyAppExePageSubHeader "Choose the installation folder for the application."
#endif

#ifndef MyAppExePageDescription
  #define MyAppExePageDescription "Select the folder where you want to install the application."
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
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
OutputBaseFilename={#MyAppOutputBase}
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "deletefiles"; Description: "Remove any existing settings (Clean installation)"; Flags: unchecked

[Files]
Source: "{#MyAppStandaloneSource}"; DestDir: "{code:GetExeInstallDir}"; Flags: ignoreversion
Source: "{#MyAppVstSource}"; DestDir: "{code:GetVstInstallDir}"; Flags: ignoreversion
Source: "{#SourcePath}..\\External\\spout\\SpoutLibrary.dll"; DestDir: "{code:GetExeInstallDir}"; Flags: ignoreversion
Source: "{#SourcePath}..\\External\\spout\\SpoutLibrary.dll"; DestDir: "{sys}"; Flags: ignoreversion

[InstallDelete]
Type: files; Name: {userappdata}\{#MyAppName}\{#MyAppName}.settings; Tasks: deletefiles

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{code:GetExeInstallDir}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{code:GetExeInstallDir}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".myp"; ValueData: ""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{code:GetExeInstallDir}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{code:GetExeInstallDir}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{code:GetExeInstallDir}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  VstDirPage: TInputDirWizardPage;
  ExeDirPage: TInputDirWizardPage;

function GetVstInstallDir(Param: string): string;
begin
  if Assigned(VstDirPage) and (Trim(VstDirPage.Values[0]) <> '') then
    Result := VstDirPage.Values[0]
  else
    Result := ExpandConstant('{#MyAppVstDefaultDir}');
end;

function GetExeInstallDir(Param: string): string;
begin
  if Assigned(ExeDirPage) and (Trim(ExeDirPage.Values[0]) <> '') then
    Result := ExeDirPage.Values[0]
  else
    Result := ExpandConstant('{autopf}') + '\' + ExpandConstant('{#MyAppName}');
end;

<event('InitializeWizard')>
procedure Product_InitializeWizard;
begin
  ExeDirPage := CreateInputDirPage(
    wpSelectDir,
    ExpandConstant('{#MyAppExePageHeader}'),
    ExpandConstant('{#MyAppExePageSubHeader}'),
    ExpandConstant('{#MyAppExePageDescription}'),
    False,
    ''
  );
  ExeDirPage.Add('');
  ExeDirPage.Values[0] := GetPreviousData('ExeInstallDir', ExpandConstant('{autopf}') + '\' + ExpandConstant('{#MyAppName}'));

  VstDirPage := CreateInputDirPage(
    ExeDirPage.ID,
    ExpandConstant('{#MyAppVstPageHeader}'),
    ExpandConstant('{#MyAppVstPageSubHeader}'),
    ExpandConstant('{#MyAppVstPageDescription}'),
    False,
    ''
  );
  VstDirPage.Add('');
  VstDirPage.Values[0] := GetPreviousData('VstInstallDir', ExpandConstant('{#MyAppVstDefaultDir}'));
end;

<event('NextButtonClick')>
function Product_NextButtonClick(CurPageID: Integer): Boolean;
var
  SelectedDir: string;
begin
  Result := True;

  if Assigned(ExeDirPage) and (CurPageID = ExeDirPage.ID) then
  begin
    SelectedDir := Trim(ExeDirPage.Values[0]);

    if SelectedDir = '' then
    begin
      MsgBox('Please select an installation folder.', mbError, MB_OK);
      Result := False;
      exit;
    end;

    if not DirExists(SelectedDir) then
    begin
      if not ForceDirectories(SelectedDir) then
      begin
        MsgBox('Unable to create the selected folder. Please choose a different location.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end;
  end;

  if Assigned(VstDirPage) and (CurPageID = VstDirPage.ID) then
  begin
    SelectedDir := Trim(VstDirPage.Values[0]);

    if SelectedDir = '' then
    begin
      MsgBox('Please select a VST3 plugin folder.', mbError, MB_OK);
      Result := False;
      exit;
    end;

    if not DirExists(SelectedDir) then
    begin
      if not ForceDirectories(SelectedDir) then
      begin
        MsgBox('Unable to create the selected folder. Please choose a different location.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end;
  end;
end;

<event('RegisterPreviousData')>
procedure Product_RegisterPreviousData(PreviousDataKey: Integer);
begin
  if Assigned(ExeDirPage) then
    SetPreviousData(PreviousDataKey, 'ExeInstallDir', ExeDirPage.Values[0]);
  if Assigned(VstDirPage) then
    SetPreviousData(PreviousDataKey, 'VstInstallDir', VstDirPage.Values[0]);
end;

<event('UpdateReadyMemo')>
function Product_UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  CustomDirInfo: String;
begin
  Result := '';
  
  if MemoUserInfoInfo <> '' then
    Result := Result + MemoUserInfoInfo + NewLine + NewLine;
  
  // Replace the default directory info with our custom directories
  CustomDirInfo := '';
  if Assigned(ExeDirPage) then
    CustomDirInfo := CustomDirInfo + 'Application will be installed to:' + NewLine + Space + ExeDirPage.Values[0] + NewLine + NewLine;
  
  if Assigned(VstDirPage) then
    CustomDirInfo := CustomDirInfo + 'VST3 Plugin will be installed to:' + NewLine + Space + VstDirPage.Values[0] + NewLine + NewLine;
  
  if CustomDirInfo <> '' then
    Result := Result + CustomDirInfo;
    
  if MemoTypeInfo <> '' then
    Result := Result + MemoTypeInfo + NewLine + NewLine;
    
  if MemoComponentsInfo <> '' then
    Result := Result + MemoComponentsInfo + NewLine + NewLine;
    
  if MemoGroupInfo <> '' then
    Result := Result + MemoGroupInfo + NewLine + NewLine;
    
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;

  // Add dependency information (manually since we disabled Dependency_UpdateReadyMemo)
  if Dependency_Memo <> '' then begin
    if MemoTasksInfo = '' then begin
      Result := Result + SetupMessage(msgReadyMemoTasks) + NewLine;
    end;
    Result := Result + FmtMessage(Dependency_Memo, [Space]);
  end;
end;

function InitializeSetup: Boolean;
begin
  Dependency_AddVC2015To2022;
  Result := True;
end;
