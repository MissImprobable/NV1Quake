; nv1Quake - emulated (software NV1) build. Compile via build-installers.ps1.
#ifndef AppVersion
  #define AppVersion "0.10"
#endif
#ifndef StageDir
  #error StageDir must be passed by build-installers.ps1
#endif
#ifndef ArtDir
  #error ArtDir must be passed by build-installers.ps1
#endif
#ifndef OutDir
  #error OutDir must be passed by build-installers.ps1
#endif
#define AppName "nv1Quake"
#define AppExeName "NV1QUAKE.EXE"

[Setup]
AppId={{6F6E7B52-2F45-4D6A-9D57-31E7B51A0A10}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} (emulated)
AppPublisher=Abnormality Software
AppPublisherURL=https://www.abnormalitysoftware.com
AppCopyright=Installer created by Abigail / Abnormality Software (2026)
DefaultDirName={autopf}\nv1Quake
DefaultGroupName=nv1Quake
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=NV1Quake-{#AppVersion}-Emulated-Setup
SetupIconFile={#ArtDir}\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
WizardImageFile={#ArtDir}\fragged-wizard.bmp
WizardSmallImageFile={#ArtDir}\fragged-wizard-small.bmp
LicenseFile={#StageDir}\COPYING.TXT
InfoBeforeFile={#StageDir}\NOTICE.TXT
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x86compatible x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\nv1Quake"; Filename: "{app}\{#AppExeName}"; Parameters: "-window"; WorkingDir: "{app}"
Name: "{autodesktop}\nv1Quake"; Filename: "{app}\{#AppExeName}"; Parameters: "-window"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Parameters: "-window"; WorkingDir: "{app}"; Description: "Launch nv1Quake"; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
; Only the PAK files the installer itself copied in. The user's saves and config.cfg also live in id1\,
; so the folder is removed only if it ends up empty.
Type: files; Name: "{app}\id1\PAK0.PAK"
Type: files; Name: "{app}\id1\PAK1.PAK"
Type: dirifempty; Name: "{app}\id1"
Type: dirifempty; Name: "{app}"

[Code]
var
  DataPage: TWizardPage;
  DataEdit: TNewEdit;

function CmdDataDir: String;
var
  I: Integer;
  P: String;
begin
  Result := '';
  for I := 1 to ParamCount do
  begin
    P := ParamStr(I);
    if CompareText(Copy(P, 1, 9), '/DATADIR=') = 0 then
      Result := Copy(P, 10, Length(P));
  end;
end;

procedure BrowseClick(Sender: TObject);
var
  Dir: String;
begin
  Dir := DataEdit.Text;
  if BrowseForFolder('Select your Quake id1 folder', Dir, False) then
    DataEdit.Text := Dir;
end;

{ A custom page rather than CreateInputDirPage: that page refuses an empty
  value, and this step must be skippable. }
procedure InitializeWizard;
var
  Info: TNewStaticText;
  Btn: TNewButton;
begin
  DataPage := CreateCustomPage(wpSelectDir, 'Quake game data', 'Where is your own copy of Quake?');

  Info := TNewStaticText.Create(DataPage);
  Info.Parent := DataPage.Surface;
  Info.AutoSize := False;
  Info.WordWrap := True;
  Info.Width := DataPage.SurfaceWidth;
  Info.Height := ScaleY(64);
  Info.Caption := 'nv1Quake does not include Quake''s game data. If you have a Quake id1 folder ' +
    '(containing PAK0.PAK and optionally PAK1.PAK), choose it and the files are copied into the ' +
    'install folder. Leave this empty to skip it and copy them in yourself later.';

  DataEdit := TNewEdit.Create(DataPage);
  DataEdit.Parent := DataPage.Surface;
  DataEdit.Top := Info.Height + ScaleY(8);
  DataEdit.Width := DataPage.SurfaceWidth - ScaleX(90);

  Btn := TNewButton.Create(DataPage);
  Btn.Parent := DataPage.Surface;
  Btn.Left := DataEdit.Width + ScaleX(8);
  Btn.Top := DataEdit.Top - 1;
  Btn.Width := ScaleX(82);
  Btn.Height := DataEdit.Height + 2;
  Btn.Caption := 'Browse...';
  Btn.OnClick := @BrowseClick;

  { Silent / scripted installs can pass /DATADIR=<folder with PAK0.PAK> }
  DataEdit.Text := CmdDataDir;
end;

function DataFolderGiven: Boolean;
begin
  Result := Trim(DataEdit.Text) <> '';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = DataPage.ID) and DataFolderGiven then
    if not FileExists(AddBackslash(Trim(DataEdit.Text)) + 'PAK0.PAK') and
       not FileExists(AddBackslash(Trim(DataEdit.Text)) + 'pak0.pak') then
    begin
      MsgBox('PAK0.PAK was not found in that folder. Choose your Quake id1 folder, or clear the box to skip.', mbError, MB_OK);
      Result := False;
    end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Src, Name: String;
  I: Integer;
  Names: array[0..1] of String;
begin
  if (CurStep = ssPostInstall) and DataFolderGiven then
  begin
    Src := AddBackslash(Trim(DataEdit.Text));
    ForceDirectories(ExpandConstant('{app}\id1'));
    Names[0] := 'PAK0.PAK'; Names[1] := 'PAK1.PAK';
    for I := 0 to 1 do
    begin
      Name := Names[I];
      if FileExists(Src + Name) then
        CopyFile(Src + Name, ExpandConstant('{app}\id1\') + Name, False)
      else if FileExists(Src + LowerCase(Name)) then
        CopyFile(Src + LowerCase(Name), ExpandConstant('{app}\id1\') + Name, False);
    end;
  end;
end;
