; Build HP3488A.exe and deploy its Qt runtime with build.ps1 before compiling.
; Install the 64-bit NI-488.2 driver separately.
#define ReadQuoted(Text) Copy(Text, Pos('"', Text) + 1, Pos('"', Copy(Text, Pos('"', Text) + 1, Len(Text))) - 1)
#define ProjectFile FileOpen(SourcePath + "CMakeLists.txt")
#expr FileRead(ProjectFile)
#define ProjectDeclaration FileRead(ProjectFile)
#define VersionTail Copy(ProjectDeclaration, Pos(" VERSION ", ProjectDeclaration) + Len(" VERSION "), Len(ProjectDeclaration))
#define HP3488AVersion Copy(VersionTail, 1, Pos(" ", VersionTail) - 1)
#expr FileClose(ProjectFile)
#define IconResource FileOpen(SourcePath + "app.rc")
#define HP3488AIcon ReadQuoted(FileRead(IconResource))
#expr FileClose(IconResource)
#ifndef BuildArch
  #define BuildArch "x64"
#endif
#if BuildArch == "x86"
  #define HP3488AName "HP 3488A Control (32 Bit)"
  #define DeviceAppId "{{A5D96A0C-C970-5462-A085-DFCC2DBD2DDF}"
  #define DeviceBuildDir "build-x86"
#elif BuildArch == "x64"
  #define HP3488AName "HP 3488A Control"
  #define DeviceAppId "{{5E805C3E-6686-4C41-A56F-20D3CFD2DA45}"
  #define DeviceBuildDir "build"
#else
  #error BuildArch must be x86 or x64.
#endif
#define HP3488AExe "HP3488A.exe"

[Setup]
AppId={#DeviceAppId}
AppName={#HP3488AName}
AppVersion={#HP3488AVersion}
AppVerName={#HP3488AName} {#HP3488AVersion}
AppPublisher=Ralf Tralow
VersionInfoVersion={#HP3488AVersion}
VersionInfoDescription={#HP3488AName} Setup
DefaultDirName={pf}\{#HP3488AName}
DefaultGroupName={#HP3488AName}
AllowNoIcons=yes
PrivilegesRequired=admin
#if BuildArch == "x64"
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#else
ArchitecturesAllowed=x86 x64
#endif
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile={#HP3488AIcon}
UninstallDisplayIcon={app}\{#HP3488AExe}
OutputDir=installer
OutputBaseFilename=HP3488A-Setup-{#HP3488AVersion}-{#BuildArch}
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
#if BuildArch == "x86"
Source: "{#DeviceBuildDir}\qt.conf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\bearer\*.dll"; DestDir: "{app}\bearer"; Flags: ignoreversion skipifsourcedoesntexist
#endif
Source: "{#DeviceBuildDir}\{#HP3488AExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion skipifsourcedoesntexist
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#HP3488AName}"; Filename: "{app}\{#HP3488AExe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#HP3488AName}"; Filename: "{app}\{#HP3488AExe}"; WorkingDir: "{app}"; Tasks: desktopicon
