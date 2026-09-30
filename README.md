
# FileZilla 3.70.6 - dark-mode fork

This repository is a fork of the **FileZilla 3.70.6** source distribution with
these additions:

1. **Native Windows dark mode** - a "Color theme" dropdown in
   *Settings > Interface > Appearance* (Follow system setting / Dark / Light).
   In rev6, clicking **OK** applies the theme to open windows without a restart.
2. **A port to wxWidgets 3.3** - upstream 3.70.6 targets wxWidgets 3.2.x; dark
   mode uses the Windows appearance support in wxWidgets 3.3.
3. **Dark-mode rendering fixes** - fixes for wxWidgets 3.3 dark-mode bugs that
   affected this build: owner-drawn checkbox/radio labels rendering blank or
   mislabeled in dialogs, file-list rows flickering on mouse hover, group box
   titles painted black with wxWidgets 3.3.3, the file list going blank
   behind the shell's delete dialog, and a "wxWidgets Debug Alert" at startup
   when the message log is shown as a tab in the transfer queue pane.



<img width="1048" height="829" alt="FileZilla-Win64-Themed" src="https://github.com/user-attachments/assets/d1110176-5ede-45b0-bc4e-240a4a983cf3" />



   

It is the FileZilla source tree only. The build dependencies (wxWidgets,
libfilezilla, fzssh, etc.) are **not** vendored here - they are external build
requirements, listed below.

Repository: https://github.com/Pharaoh2k/FileZilla-Themed-For-Windows

## License

FileZilla is licensed under the **GNU GPL, version 3 or later version**. See [LICENSE](LICENSE)
for the full GPLv3 text. The changes in this fork are likewise GPLv3-or-later.

## What changed (the fork)

Git history makes this explicit:

- Commit 1 - *"Import pristine FileZilla 3.70.6 source"* - the unmodified
  upstream tarball.
- Commit 2 - *"Fork: add Windows dark mode + port to wxWidgets 3.3"* - the
  entire diff (12 files).

See [CHANGES.fork.md](CHANGES.fork.md) for the per-file list required by the GPL.

### Dark mode

- New option `OPTION_APPEARANCE_MODE` (0 = follow system, 1 = dark, 2 = light).
- `CFileZillaApp::ApplyAppearanceMode()` applies the saved theme at startup and
  applies changes after **OK** in Settings. Cancel leaves the theme unchanged.
- Open windows are updated in place. Connections, transfer queues, and pane
  selections are retained. **Follow system setting** also responds when the
  Windows app colour preference changes.
- Stock wxWidgets 3.3.3 handles Windows system-theme changes, but its public
  `SetAppearance()` still rejects manual changes after windows exist. The
  bundled `wx333-runtime-appearance.patch` enables the same update mechanism
  for the Light/Dark/System selector; rebuilding requires both wx patches.
  See the [upstream discussion](https://github.com/wxWidgets/wxWidgets/pull/26516).

### wxWidgets 3.3 port fixes

- `configure` / `configure.ac`: relax the hard "must use wxWidgets 3.2.x" gate.
- `aui_notebook_ex.cpp`: `GetTabSize` override `wxDC&` -> `wxReadOnlyDC&`; and
  for wxWidgets 3.3.2, drop the base `wxAuiNotebook::OnTabDragMotion(evt)` call
  (3.3.2 made tab-drag handling internal and runs the default before posting the
  event, so chaining to the old base handler no longer compiles).
- `fileexistsdlg.cpp`: `wxIcon` `SetHandle`/`SetSize` -> `InitFromHICON`.
- `LocalTreeView.cpp`, `sitemanager_controls.cpp`,
  `settings/optionspage_filetype.cpp`: explicit wide-char/string literals
  (wxWidgets 3.3 removed `wxString`'s implicit narrow conversions).
- `file_utils.cpp`: `CallSHFileOperation` used to hide the helper window it
  creates over the file list or tree (to run the shell's delete/rename/copy
  dialog in a modal event loop) with `wxTRANSPARENT_WINDOW`. That style is a
  no-op since wxWidgets 3.3, so the helper became an opaque child window painted
  in the window background colour and the list or tree went blank as soon as
  the confirmation dialog appeared. The helper is now created hidden and
  size-less, which works with every wxWidgets version.
- `Mainfrm.cpp`: with the message log positioned *as a tab in the transfer
  queue pane*, the status view was created as a child of a splitter and then
  passed to `wxAuiNotebook::AddPage`, relying on wxWidgets to reparent it.
  wxWidgets 3.3.3 stopped doing that and asserts *"page must be a child of the
  notebook"* instead, which showed a "wxWidgets Debug Alert" at startup and
  left the Message log tab missing. The status view is now created as a child
  of the queue notebook (and reparented to it when the position is changed at
  run time).

### Dark-mode rendering fixes

- `src/interface/filelistctrl.cpp`: don't force `wxBG_STYLE_SYSTEM` in dark
  mode (it overrode the double-buffered `wxBG_STYLE_PAINT` wxWidgets uses),
  which fixes file-list rows flickering on mouse hover.
- `src/interface/FileZilla.cpp`: enable wxWidgets' own static box painting
  (`msw.staticbox.optimized-paint`) from startup in both themes. This keeps
  group-box headings readable when an existing window changes to dark mode.
- Runtime changes also refresh Windows popup-menu caches and cached transfer
  progress bitmaps, and preserve the colours of tinted tabs.
- Colour-change handlers let wxWidgets update native controls before reapplying
  file-list colours, site tints, and message-log colours.
- A **wxWidgets patch** (applied to the wxWidgets 3.3.3 source - not vendored
  here - see [patches/](patches/) and [BUILD.md](BUILD.md)) fixes owner-drawn
  checkboxes/radio buttons rendering blank in dark dialogs (e.g. *File > Export
  settings*, the Settings dialog):
  - `src/msw/window.cpp`: route `WM_DRAWITEM` to the control by its HWND instead
    of by id. FileZilla creates many controls with the same id (`nullID =
    wxID_HIGHEST`), so the id-based lookup returned the wrong control (often a
    non-owner-drawn static text), leaving the real checkbox unpainted.

  This is really a consequence of reusing one id across controls, which
  wxWidgets does not expect; the HWND lookup just makes it robust against that.
  An equally valid fix would be to give those controls unique ids on the
  FileZilla side.

## Known issues

The rev1-rev5 themed packages omitted the Explorer shell extension, preventing
remote files and folders from being dragged onto the Windows desktop or into
Explorer on systems without a registered extension. Rev6 restores the DLLs and
adds a checked **Shell Extension** installer component. Installation and remote
drag-and-drop were verified on the test machine. Extracting the portable ZIP
alone does not register the extension.
See [CHANGELOG.md](CHANGELOG.md) for release history.

Rev6 adds live theme switching with a bundled wxWidgets patch. It refreshes
cached popup-menu and transfer-row colours and open filter/search dialog
backgrounds. The final build passed maintainer testing after a regression sweep
covering menus, Settings pages, major dialogs, and FTP/FTPS/SFTP transfers.

## Build requirements

Built and verified on Windows with an MSYS2 mingw64 toolchain (gcc 16.1).

- **wxWidgets 3.3.3** (built from source; >= 3.3 is required for dark mode,
  >= 3.3.2 for `aui_notebook_ex.cpp`; apply both wxWidgets patches in
  [patches/](patches/) before building - see [BUILD.md](BUILD.md))
- **libfilezilla 0.56.1** (>= 0.56.1)
- **fzssh 1.3.0** / libfzssh-client (>= 1.3.0)
- Boost (Boost.Regex >= 1.76), nettle, gnutls, gmp, argon2, sqlite3, gettext,
  libidn2
- pkgconf, meson, ninja
- For the 32-bit Explorer shell extension: a 32-bit (i686) mingw gcc

## Building (Windows / MSYS2)

**See [BUILD.md](BUILD.md) for the complete, verified from-scratch recipe** -
dependency build order (libfilezilla, fzssh, wxWidgets 3.3.3), the wx-config
wrapper, the Explorer shell extension, translation catalogs, and the
environment workarounds, plus the why behind each one.

The essentials:

```sh
# Out-of-tree build dir (kept out of the repo via .gitignore)
mkdir -p compile && cd compile

export PATH=/c/Users/Pharaoh/Downloads/fzbuild/bin:/c/msys64/mingw64/bin:/usr/bin:/bin
export PKG_CONFIG_PATH=/c/msys64/mingw64/lib/pkgconfig
export TMP='C:/Users/Pharaoh/AppData/Local/Temp' TEMP='C:/Users/Pharaoh/AppData/Local/Temp'

../configure --prefix=C:/msys64/mingw64 --disable-static \
  --disable-manualupdatecheck --disable-dependency-tracking MAKE=mingw32-make \
  --with-wx-config=/c/Users/Pharaoh/Downloads/fzbuild/bin/wx-config \
  --with-pugixml=builtin

mingw32-make -j$(nproc) SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe' \
  CXXFLAGS="-g -O2 -pipe" CFLAGS="-g -O2 -pipe"
mingw32-make install SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'
```

The four workarounds (why they are needed is in the build notes):

1. Build with **native** `mingw32-make`, not the MSYS `make`.
2. Set the recipe `SHELL` to Git's healthy `sh` via an 8.3 short path:
   `SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'`.
3. Use `-pipe` and Windows-form `TMP`/`TEMP` (avoids a non-writable
   `C:\WINDOWS` temp fallback).
4. Use the `wx-config` wrapper that forces the wxWidgets 3.3 prefix.

Resulting binary: `C:/msys64/mingw64/bin/filezilla.exe`; resources under
`share/filezilla/`; config in `%APPDATA%\FileZilla`.

## Upstream

FileZilla - https://filezilla-project.org/ - Copyright (C) Tim Kosse and
contributors. See [AUTHORS](AUTHORS).
