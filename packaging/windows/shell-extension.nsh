; FileZilla Themed packaging, Pharaoh2k, 2026-09-30, GPL-3.0-or-later.
!ifndef FZ_THEMED_SHELL_EXTENSION
!define FZ_THEMED_SHELL_EXTENSION
!include LogicLib.nsh

; These defaults can be redirected to private keys by the regression harness.
!ifndef SHELL_ROOT
!define SHELL_ROOT HKLM
!endif
!ifndef SHELL_CLASSES
!define SHELL_CLASSES "Software\Classes"
!endif
!ifndef SHELL_SETTINGS
!define SHELL_SETTINGS "Software\FileZilla 3\fzshellext"
!endif
!define SHELL_CLSID "{DB70412E-EEC9-479C-BBA9-BE36BFDDA41B}"
!define SHELL_CLASS "${SHELL_CLASSES}\CLSID\${SHELL_CLSID}"
!ifndef SHELL_HOOK
!define SHELL_HOOK "${SHELL_CLASSES}\Directory\shellex\CopyHookHandlers\FileZilla3CopyHook"
!endif

; Mirror RegisterServer in src/fzshellext/shellext.cpp explicitly. This avoids
; loading a DLL of the wrong architecture into the installer and lets uninstall
; remove only our machine registrations. DllUnregisterServer removes both HKLM
; and HKCU unconditionally, including registrations owned by other installs.
!macro RegisterShellExtension VIEW DLL
  SetRegView ${VIEW}
  ReadRegStr $0 ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" ""
  ${If} $0 != "$INSTDIR\${DLL}"
    ; Keep this backup across repair installs and upgrades of our own DLL.
    WriteRegStr ${SHELL_ROOT} "${REGAPP}" "PreviousShellExtension${VIEW}" "$0"
  ${EndIf}
  ClearErrors
  WriteRegStr ${SHELL_ROOT} "${SHELL_CLASS}" "" "FileZilla 3 Shell Extension"
  WriteRegStr ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" "" "$INSTDIR\${DLL}"
  WriteRegStr ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" "ThreadingModel" "Apartment"
  WriteRegStr ${SHELL_ROOT} "${SHELL_HOOK}" "" "${SHELL_CLSID}"
  WriteRegDWORD ${SHELL_ROOT} "${SHELL_SETTINGS}" "Enable" 1
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Could not register the ${VIEW}-bit Shell Extension. Run Setup again as an administrator." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
!macroend

!macro UnregisterShellExtension VIEW DLL
  SetRegView ${VIEW}
  ReadRegStr $0 ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" ""
  ${If} $0 == "$INSTDIR\${DLL}"
    ReadRegStr $1 ${SHELL_ROOT} "${REGAPP}" "PreviousShellExtension${VIEW}"
    ${If} $1 != ""
    ${AndIf} ${FileExists} "$1"
      ; Restore an earlier FileZilla installation if its DLL still exists.
      WriteRegStr ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" "" "$1"
    ${Else}
      DeleteRegKey ${SHELL_ROOT} "${SHELL_CLASS}"
      DeleteRegValue ${SHELL_ROOT} "${SHELL_SETTINGS}" "Enable"
      DeleteRegKey /ifempty ${SHELL_ROOT} "${SHELL_SETTINGS}"
    ${EndIf}
  ${EndIf}
  DeleteRegValue ${SHELL_ROOT} "${REGAPP}" "PreviousShellExtension${VIEW}"
!macroend

!macro RemoveUnusedShellHook
  ; Directory associations are shared between registry views, CLSIDs are not.
  ; Leave the handler in place if either architecture belongs to another copy.
  SetRegView 32
  ReadRegStr $0 ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" ""
  SetRegView 64
  ReadRegStr $1 ${SHELL_ROOT} "${SHELL_CLASS}\InProcServer32" ""
  ${If} $0 == ""
  ${AndIf} $1 == ""
    ReadRegStr $2 ${SHELL_ROOT} "${SHELL_HOOK}" ""
    ${If} $2 == "${SHELL_CLSID}"
      DeleteRegValue ${SHELL_ROOT} "${SHELL_HOOK}" ""
      DeleteRegKey /ifempty ${SHELL_ROOT} "${SHELL_HOOK}"
    ${EndIf}
  ${EndIf}
!macroend
!endif
