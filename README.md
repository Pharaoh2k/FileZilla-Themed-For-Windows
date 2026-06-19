# FileZilla 3.70.6 - dark-mode fork

This repository is a fork of the **FileZilla 3.70.6** source distribution with
two additions:

1. **Native Windows dark mode** - a "Color theme" dropdown in
   *Settings > Interface > Appearance* (Follow system setting / Dark / Light).
2. **A port to wxWidgets 3.3** - upstream 3.70.6 targets wxWidgets 3.2.x; dark
   mode needs `wxApp::MSWEnableDarkMode()`, which exists only in wxWidgets >= 3.3.
3. **Dark-mode rendering fixes** - fixes for wxWidgets 3.3 dark-mode bugs that
   affected this build: owner-drawn checkbox/radio labels rendering blank or
   mislabeled in dialogs, and file-list rows flickering on mouse hover.



<img width="1186" height="943" alt="FileZilla-Win64-Themed" src="https://github.com/user-attachments/assets/be659053-52bc-4fad-8d01-c36dfe334e38" />




   

It is the FileZilla source tree only. The build dependencies (wxWidgets,
libfilezilla, fzssh, etc.) are **not** vendored here - they are external build
requirements, listed below.

Mirror: https://git.slowb.ro/Pharaoh2k/FileZilla-Themed-For-Windows

## License

FileZilla is licensed under the **GNU GPL, version 3 or (at your option) any
later version**. This fork keeps that license unchanged. See [LICENSE](LICENSE)
for the full GPLv3 text. The changes in this fork are likewise GPLv3-or-later.

GPLv3+ code cannot be relicensed to GPLv2-only, so this fork is **not** GPLv2.

## What changed (the fork)

Git history makes this explicit:

- Commit 1 - *"Import pristine FileZilla 3.70.6 source"* - the unmodified
  upstream tarball.
- Commit 2 - *"Fork: add Windows dark mode + port to wxWidgets 3.3"* - the
  entire diff (12 files).

See [CHANGES.fork.md](CHANGES.fork.md) for the per-file list required by the GPL.

### Dark mode

- New option `OPTION_APPEARANCE_MODE` (0 = follow system, 1 = dark, 2 = light).
- `CFileZillaApp::ApplyAppearanceMode()` applies the theme in `OnInit()` before
  any windows are created.
- The theme is applied **at startup only**. Changing the dropdown shows
  *"The color theme will be applied the next time FileZilla is started."*
  (live switching was tried and intentionally dropped).

### wxWidgets 3.3 port fixes

- `configure` / `configure.ac`: relax the hard "must use wxWidgets 3.2.x" gate.
- `aui_notebook_ex.cpp`: `GetTabSize` override `wxDC&` -> `wxReadOnlyDC&`.
- `fileexistsdlg.cpp`: `wxIcon` `SetHandle`/`SetSize` -> `InitFromHICON`.
- `LocalTreeView.cpp`, `sitemanager_controls.cpp`,
  `settings/optionspage_filetype.cpp`: explicit wide-char/string literals
  (wxWidgets 3.3 removed `wxString`'s implicit narrow conversions).

### Dark-mode rendering fixes

- `src/interface/filelistctrl.cpp`: don't force `wxBG_STYLE_SYSTEM` in dark
  mode (it overrode the double-buffered `wxBG_STYLE_PAINT` wxWidgets uses),
  which fixes file-list rows flickering on mouse hover.
- A **wxWidgets 3.3.1 patch** (applied to the wxWidgets source - not vendored
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

None currently. The earlier dark-mode rendering bugs (invisible/mislabeled
owner-drawn checkboxes and file-list hover flicker) have been fixed - see
*Dark-mode rendering fixes* above.

## Build requirements

Built and verified on Windows with an MSYS2 mingw64 toolchain (gcc 16.1).

- **wxWidgets 3.3.1** (built from source; >= 3.3 is required for dark mode;
  apply the dark-mode patches in [patches/](patches/) before building - see
  [BUILD.md](BUILD.md))
- **libfilezilla 0.56.1** (>= 0.56.1)
- **fzssh 1.3.0** / libfzssh-client (>= 1.3.0)
- Boost (Boost.Regex >= 1.76), nettle, gnutls, gmp, argon2, sqlite3, gettext,
  libidn2
- pkgconf, meson, ninja
- For the 32-bit Explorer shell extension: a 32-bit (i686) mingw gcc

## Building (Windows / MSYS2)

**See [BUILD.md](BUILD.md) for the complete, verified from-scratch recipe** -
dependency build order (libfilezilla, fzssh, wxWidgets 3.3.1), the wx-config
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
