; FileZilla Themed installer, Pharaoh2k, 2026-09-30, GPL-3.0-or-later.
; Invoke through build-release.ps1 so missing DLLs fail before compilation.
Unicode true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "Library.nsh"

!define APPNAME "FileZilla Themed"
!define APPVER "3.70.6"
!ifndef REVISION
!define REVISION 6
!endif
!ifndef SRCDIR
!error "SRCDIR must point to the validated release bundle"
!endif
!ifndef RELEASEDIR
!error "RELEASEDIR must point to the release output directory"
!endif
!define MAINEXE "filezilla.exe"
!define REGUNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\FileZillaThemed"
!define REGAPP "Software\FileZilla Themed"
!include "shell-extension.nsh"

Name "${APPNAME} ${APPVER} rev${REVISION}"
OutFile "${RELEASEDIR}\FileZilla-Themed-${APPVER}-win64-setup-rev${REVISION}.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "${REGAPP}" "InstallDir"
RequestExecutionLevel admin
VIProductVersion "3.70.6.${REVISION}"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileVersion" "${APPVER} rev${REVISION}"
VIAddVersionKey "ProductVersion" "${APPVER} rev${REVISION}"
VIAddVersionKey "CompanyName" "Pharaoh2k"
VIAddVersionKey "LegalCopyright" "GNU GPL v3 or later"
VIAddVersionKey "FileDescription" "${APPNAME} Setup"

!define MUI_ICON "..\..\src\interface\resources\FileZilla.ico"
!define MUI_UNICON "..\..\data\uninstall.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${MAINEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${APPNAME}"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${IsNativeAMD64}
    MessageBox MB_OK|MB_ICONSTOP "This build requires an x64 Windows PC." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "FileZilla Client" SecMain
  SectionIn RO
  SetRegView 64
  SetOutPath "$INSTDIR"
  ; Shell DLLs must be installed separately to handle files locked by Explorer.
  File /r /x fzshellext.dll /x fzshellext_64.dll /x locales "${SRCDIR}\*.*"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${MAINEXE}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${REGAPP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${REGUNINST}" "DisplayName" "${APPNAME} ${APPVER} rev${REVISION}"
  WriteRegStr HKLM "${REGUNINST}" "DisplayVersion" "${APPVER} rev${REVISION}"
  WriteRegStr HKLM "${REGUNINST}" "Publisher" "Pharaoh2k"
  WriteRegStr HKLM "${REGUNINST}" "DisplayIcon" "$INSTDIR\${MAINEXE}"
  WriteRegStr HKLM "${REGUNINST}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${REGUNINST}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
  WriteRegStr HKLM "${REGUNINST}" "QuietUninstallString" '$\"$INSTDIR\uninstall.exe$\" /S'
  WriteRegDWORD HKLM "${REGUNINST}" "NoModify" 1
  WriteRegDWORD HKLM "${REGUNINST}" "NoRepair" 1
SectionEnd

Section "Language files" SecLang
  SetOutPath "$INSTDIR\locales"
  File /r "${SRCDIR}\locales\*.*"
SectionEnd

Section "Shell Extension" SecShell
  SetOutPath "$INSTDIR"
  !define LIBRARY_IGNORE_VERSION
  !insertmacro InstallLib DLL NOTSHARED REBOOT_NOTPROTECTED "${SRCDIR}\fzshellext.dll" "$INSTDIR\fzshellext.dll" "$INSTDIR"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_NOTPROTECTED "${SRCDIR}\fzshellext_64.dll" "$INSTDIR\fzshellext_64.dll" "$INSTDIR"
  !undef LIBRARY_IGNORE_VERSION
  !insertmacro RegisterShellExtension 32 fzshellext.dll
  !insertmacro RegisterShellExtension 64 fzshellext_64.dll
  System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Desktop Icon" SecDesktop
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${MAINEXE}"
SectionEnd

Section -Finish
  SetRegView 64
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${REGUNINST}" "EstimatedSize" "$0"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
!insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "FileZilla with Windows dark mode support."
!insertmacro MUI_DESCRIPTION_TEXT ${SecLang} "FileZilla translations."
!insertmacro MUI_DESCRIPTION_TEXT ${SecShell} "Enables dragging remote files and folders to the Windows desktop and Explorer."
!insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a desktop shortcut."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  !insertmacro UnregisterShellExtension 32 fzshellext.dll
  !insertmacro UnregisterShellExtension 64 fzshellext_64.dll
  !insertmacro RemoveUnusedShellHook
  System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  ; Explorer may keep either extension loaded until the next restart.
  !insertmacro UnInstallLib DLL NOTSHARED REBOOT_NOTPROTECTED "$INSTDIR\fzshellext.dll"
  !insertmacro UnInstallLib DLL NOTSHARED REBOOT_NOTPROTECTED "$INSTDIR\fzshellext_64.dll"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  SetOutPath "$TEMP"
  RMDir /r /REBOOTOK "$INSTDIR"
  SetRegView 32
  DeleteRegKey /ifempty HKLM "${REGAPP}"
  SetRegView 64
  DeleteRegKey HKLM "${REGUNINST}"
  DeleteRegKey HKLM "${REGAPP}"
SectionEnd
