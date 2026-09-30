# Modifications to FileZilla 3.70.6

This file records the changes made to the upstream FileZilla 3.70.6 source, as
required by the GNU GPL (section 5: modified files must carry prominent notices
stating that they were changed, and the date).

- **Base version:** FileZilla 3.70.6 (upstream source distribution)
- **Modified by:** Pharaoh2k (https://github.com/Pharaoh2k)
- **Date of changes:** 2026-06-06; dark-mode rendering fixes 2026-06-07;
  wxWidgets 3.3.3 upgrade, delete-dialog and static box fixes 2026-09-03;
  message-log-as-tab startup assert fix 2026-09-04;
  shell-extension release packaging and live theme switching 2026-09-30
- **License:** unchanged - GNU GPL v3 or (at your option) any later version

## Summary

Added native Windows dark mode and ported the source to wxWidgets 3.3 (upstream
3.70.6 targets wxWidgets 3.2.x), plus fixes for wxWidgets 3.3 dark-mode
rendering bugs. See [README.md](README.md) for details and build instructions.

## Source files changed

Dark mode feature:

| File | Change |
| --- | --- |
| `src/interface/Options.h` | Add `OPTION_APPEARANCE_MODE` to the option enum |
| `src/interface/Options.cpp` | Register the `Appearance mode` option (clamp 0-2) |
| `src/interface/filezillaapp.h` | Declare `CFileZillaApp::ApplyAppearanceMode()` |
| `src/interface/FileZilla.cpp` | Apply the saved appearance at startup and on changes; enable wx static-box painting in both themes so headings remain readable during live switching |
| `src/interface/settings/optionspage_interface.cpp` | "Color theme" dropdown; defer applying changed settings until the dialog closes, without restarting |

wxWidgets 3.3 port fixes:

| File | Change |
| --- | --- |
| `configure` | Relax the "must use wxWidgets 3.2.x" version gate |
| `configure.ac` | Relax the "must use wxWidgets 3.2.x" version gate |
| `src/interface/aui_notebook_ex.cpp` | Restore the saved active-tab colour after drawing a site tint;  `GetTabSize`: `wxDC&` -> `wxReadOnlyDC&`; drop base `OnTabDragMotion(evt)` call (removed in wx 3.3.2) |
| `src/interface/fileexistsdlg.cpp` | `wxIcon` `SetHandle`/`SetSize` -> `InitFromHICON` |
| `src/interface/LocalTreeView.cpp` | Explicit `wchar_t` cast (no implicit wxString narrow conversion) |
| `src/interface/sitemanager_controls.cpp` | Wide string literals (`L"..."`) for comparisons |
| `src/interface/settings/optionspage_filetype.cpp` | Wide char literal (`L'|'`) |
| `src/interface/file_utils.cpp` | `CallSHFileOperation`: create the modal-loop helper window hidden and size-less instead of relying on `wxTRANSPARENT_WINDOW` (a no-op since wx 3.3); fixes the local file list or tree going blank behind the shell's delete/rename dialog |
| `src/interface/Mainfrm.cpp` | Message log position "as tab in the queue pane": create the status view as a child of the queue notebook, and `Reparent` it there before `AddPage` when the position is changed at run time. wxWidgets 3.3.3 no longer reparents pages in `wxAuiNotebook::InsertPage` and asserts "page must be a child of the notebook" instead, which showed a debug alert at startup and left the Message log tab missing |

Dark-mode rendering fix:

| File | Change |
| --- | --- |
| `src/interface/statuslinectrl.cpp` | Refresh progress-row foreground/background and invalidate its bitmap cache when the theme changes |
| `src/interface/customheightlistctrl.cpp` | Refresh the explicit filter/search conditions background when an open dialog changes theme |
| `src/interface/filelistctrl.cpp` | Use the correct background style both at startup and when switching themes (fixes file-list hover flicker) |
| `src/interface/infotext.cpp` | Draw empty-list status text using the current list foreground after theme changes |
| `src/interface/graphics.cpp` | Propagate system-colour changes before reapplying site tints, so native child controls also update |
| `src/interface/StatusView.cpp`, `src/interface/StatusView.h` | Propagate colour changes to the message-log control before refreshing text attributes; recolour existing log entries by message type while preserving selection and scroll position |

The dark mode + wxWidgets 3.3 port diff is the commit *"Fork: add Windows dark
mode + port to wxWidgets 3.3"*; the first commit is the unmodified upstream
3.70.6 source.

## Release packaging (rev6, 2026-09-30)

Added the tracked PowerShell release builder, NSIS installer and registration
helpers under `packaging/windows`. Both shell
extension architectures are explicitly packaged, and the installer has a
selected **Shell Extension** component. Registration and removal track
ownership so another FileZilla installation's registration is preserved.

Updated the build instructions and README, added a release changelog and notes,
and included license documents and release notes in the downloads. The release
description includes checksums; GitHub supplies source archives from its tag.
Rev6 also rebuilds FileZilla and the wxWidgets core DLL for live theme
switching. Settings applies the selected mode after saving all pages and
closing the dialog, without recreating windows.

## wxWidgets patches (not part of this source tree)

Both patches live in [patches/](patches/) and must be applied to the external
wxWidgets 3.3.3 source before building (see [BUILD.md](BUILD.md)):

- `patches/wx333-darkmode-ownerdrawn-fixes.patch`
  - `src/msw/window.cpp`: route `WM_DRAWITEM` to the owner-drawn control by its
    HWND instead of by id, so it still works when several controls share one id
    (FileZilla uses `nullID = wxID_HIGHEST` for everything). Without it the draw
    is dispatched to the wrong control and the checkbox renders blank.

- `patches/wx333-runtime-appearance.patch` (2026-09-30)
  - `src/msw/darkmode.cpp`: allow application-requested Light/Dark/System
    changes with existing windows. Update the preferred mode, mark the theme
    transition, and notify top-level windows through the existing native
    system-colour path. Snapshot weak references in case a handler destroys a
    window. Preserve custom dark-mode settings and startup-only bookkeeping. Flush the
    Windows popup-menu theme cache after setting the preferred app mode, so
    menus previously opened in dark mode render correctly after switching to light.
