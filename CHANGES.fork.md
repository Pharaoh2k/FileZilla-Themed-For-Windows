# Modifications to FileZilla 3.70.6

This file records the changes made to the upstream FileZilla 3.70.6 source, as
required by the GNU GPL (section 5: modified files must carry prominent notices
stating that they were changed, and the date).

- **Base version:** FileZilla 3.70.6 (upstream source distribution)
- **Modified by:** Pharaoh2k (https://github.com/Pharaoh2k)
- **Date of changes:** 2026-06-06; dark-mode rendering fixes 2026-06-07
- **License:** unchanged - GNU GPL v3 or (at your option) any later version

## Summary

Added native Windows dark mode and ported the source to wxWidgets 3.3 (upstream
3.70.6 targets wxWidgets 3.2.x), plus fixes for wxWidgets 3.3 dark-mode
rendering bugs. See [README.md](README.md) for details and build instructions.

## Files changed (13)

Dark mode feature:

| File | Change |
| --- | --- |
| `src/interface/Options.h` | Add `OPTION_APPEARANCE_MODE` to the option enum |
| `src/interface/Options.cpp` | Register the `Appearance mode` option (clamp 0-2) |
| `src/interface/filezillaapp.h` | Declare `CFileZillaApp::ApplyAppearanceMode()` |
| `src/interface/FileZilla.cpp` | Define `ApplyAppearanceMode()`; call it in `OnInit()` |
| `src/interface/settings/optionspage_interface.cpp` | "Color theme" dropdown + restart-required message on change |

wxWidgets 3.3 port fixes:

| File | Change |
| --- | --- |
| `configure` | Relax the "must use wxWidgets 3.2.x" version gate |
| `configure.ac` | Relax the "must use wxWidgets 3.2.x" version gate |
| `src/interface/aui_notebook_ex.cpp` | `GetTabSize`: `wxDC&` -> `wxReadOnlyDC&` |
| `src/interface/fileexistsdlg.cpp` | `wxIcon` `SetHandle`/`SetSize` -> `InitFromHICON` |
| `src/interface/LocalTreeView.cpp` | Explicit `wchar_t` cast (no implicit wxString narrow conversion) |
| `src/interface/sitemanager_controls.cpp` | Wide string literals (`L"..."`) for comparisons |
| `src/interface/settings/optionspage_filetype.cpp` | Wide char literal (`L'|'`) |

Dark-mode rendering fix:

| File | Change |
| --- | --- |
| `src/interface/filelistctrl.cpp` | Don't force `wxBG_STYLE_SYSTEM` in dark mode (fixes file-list hover flicker) |

The dark mode + wxWidgets 3.3 port diff is the commit *"Fork: add Windows dark
mode + port to wxWidgets 3.3"*; the first commit is the unmodified upstream
3.70.6 source.

## wxWidgets patches (not part of this source tree)

The owner-drawn checkbox/radio dark-mode bug is fixed in **wxWidgets itself**,
not in FileZilla. The patch lives in [patches/](patches/) and must be applied
to the wxWidgets 3.3.1 source before building (see [BUILD.md](BUILD.md)):

- `patches/wx331-darkmode-ownerdrawn-fixes.patch`
  - `src/msw/window.cpp`: route `WM_DRAWITEM` to the owner-drawn control by its
    HWND instead of by id, so it still works when several controls share one id
    (FileZilla uses `nullID = wxID_HIGHEST` for everything). Without it the draw
    is dispatched to the wrong control and the checkbox renders blank.
