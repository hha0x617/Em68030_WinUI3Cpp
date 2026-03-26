#define MyAppName "Em68030"
#define MyAppPublisher "hha0x617"
#define MyAppExeName "Em68030-WinUI3.exe"

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef SourceDir
  #define SourceDir "..\release"
#endif

#ifndef Arch
  #define Arch "x64"
#endif

[Setup]
AppId={{A7E2B1C3-6D4F-4A5E-8F7B-2A3B4C5D6E7F}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}-WinUI3
DefaultGroupName={#MyAppName}-WinUI3
DisableProgramGroupPage=yes
OutputBaseFilename=Em68030-WinUI3-Setup-{#Arch}-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\Em68030.exe"; DestDir: "{app}"; DestName: "{#MyAppExeName}"; Flags: ignoreversion
Source: "{#SourceDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.pri"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.xbf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\Assets\*"; DestDir: "{app}\Assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Strings\*"; DestDir: "{app}\Strings"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Microsoft.UI.Xaml\*"; DestDir: "{app}\Microsoft.UI.Xaml"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\en-US\*"; DestDir: "{app}\en-US"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\ja-JP\*"; DestDir: "{app}\ja-JP"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}-WinUI3"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
