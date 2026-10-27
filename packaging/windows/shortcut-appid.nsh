; FileZilla Themed packaging, Pharaoh2k, 2026-10-27, GPL-3.0-or-later.
!ifndef FZ_THEMED_SHORTCUT_APPID
!define FZ_THEMED_SHORTCUT_APPID
!include LogicLib.nsh

; filezilla.exe sets this explicit AppUserModelID at startup. Windows attributes
; its notifications to the app only through a shortcut carrying the same ID;
; without one they appear under an anonymous sender. Upstream sets it with its
; nsis_appid plugin; this uses IShellLinkW, IPropertyStore and IPersistFile.
!define APPUSERMODELID "FileZilla.Client.AppID"

!macro SetShortcutAppID LNK
  System::Store S
  StrCpy $9 "${LNK}"
  System::Call 'ole32::CoInitialize(p 0) i .r8'
  System::Call 'ole32::CoCreateInstance(g "{00021401-0000-0000-C000-000000000046}", p 0, i 1, g "{000214F9-0000-0000-C000-000000000046}", *p .r1) i .r0'
  ${If} $0 = 0
    System::Call '$1->0(g "{0000010B-0000-0000-C000-000000000046}", *p .r2) i .r0'
    ${If} $0 = 0
      System::Call '$2->5(w r9, i 2) i .r0'
      ${If} $0 = 0
        System::Call '$1->0(g "{886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99}", *p .r3) i .r0'
        ${If} $0 = 0
          ; PKEY_AppUserModel_ID and a VT_LPWSTR PROPVARIANT pointing at the ID
          System::Call '*(&g16 "{9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}", i 5) p .r4'
          System::Call '*(&w64 "${APPUSERMODELID}") p .r5'
          System::Call '*(&i2 31, &i2 0, &i2 0, &i2 0, p r5, p 0) p .r6'
          System::Call '$3->6(p r4, p r6) i .r0'
          ${If} $0 = 0
            System::Call '$3->7() i .r0'
          ${EndIf}
          ${If} $0 = 0
            System::Call '$2->6(p 0, i 1) i .r0'
          ${EndIf}
          System::Free $6
          System::Free $5
          System::Free $4
          System::Call '$3->2()'
        ${EndIf}
      ${EndIf}
      System::Call '$2->2()'
    ${EndIf}
    System::Call '$1->2()'
  ${EndIf}
  ${If} $0 <> 0
    DetailPrint "Could not set the AppUserModelID on $9 (HRESULT $0)."
  ${EndIf}
  ${If} $8 >= 0
    System::Call 'ole32::CoUninitialize()'
  ${EndIf}
  System::Store L
!macroend

!endif
