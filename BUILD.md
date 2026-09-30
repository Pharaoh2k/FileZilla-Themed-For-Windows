# Building this fork from source (Windows / MSYS2)

This is the complete, verified recipe for building FileZilla 3.70.6 (this
dark-mode fork) from source on Windows using an MSYS2 mingw64 toolchain. It
includes the dependency build order, the wxWidgets 3.3 build, the Explorer shell
extension, the translation catalogs, and the environment workarounds that this
build needs.

> Paths such as `C:/Users/Pharaoh/Downloads/fzbuild` are the layout used on the
> machine this was developed on. Adjust them to your own scratch directory; the
> important parts are the flags, the build order, and the four workarounds.

## 0. Why the workarounds exist (read this first)

The development machine had a **split toolchain**, and the same conditions are
easy to hit on any Windows box that has both MSYS2 and Git-for-Windows
installed. Understanding this saves hours:

- **`C:\msys64`** (MSYS2) has the mingw64/mingw32 compilers, libraries, and
  `mingw32-make`. On the dev machine its **msys2 runtime was corrupted**:
  `/usr/bin` `expr`/`sed`/`grep` had broken BRE capture groups, which silently
  breaks autoconf (e.g. exeext detection). Its `/bin/sh` also mangles
  `TMP`/`TEMP` for native child processes, so the compiler falls back to a
  non-writable `C:\WINDOWS` for temp files.
- **Git for Windows** (`C:\Program Files\Git`) has healthy `/usr/bin` coreutils
  (sed/grep/expr/sh) but no compiler and no make. It also ships its own
  `/mingw64` tree, which makes a bare `/mingw64` ambiguous (`wx-config` can
  resolve to Git's empty tree).

### The four workarounds (mandatory)

1. **Build with native `mingw32-make`, never the MSYS `make`.** Pass
   `MAKE=mingw32-make` to every `configure`.
2. **Point the recipe `SHELL` at Git's healthy `sh`, via an 8.3 short path**
   (the space in "Program Files" breaks `$(SHELL)`):
   `SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'`.
3. **Use `-pipe` and Windows-form `TMP`/`TEMP`** (forward slashes) so temp
   files do not fall back to `C:\WINDOWS`.
4. **Use a `wx-config` wrapper that forces the wxWidgets 3.3 prefix**, to avoid
   the dual-`/mingw64` ambiguity (see step 4).

If your MSYS2 runtime is healthy you may not need 1-3, but they are harmless.

## 1. Canonical build environment

Use this environment for every autotools step (a Git-bash / MSYS shell):

```sh
export PATH=/c/Users/Pharaoh/Downloads/fzbuild/bin:/c/msys64/mingw64/bin:/usr/bin:/bin
export PKG_CONFIG_PATH=/c/msys64/mingw64/lib/pkgconfig
# WINDOWS-form, forward slashes:
export TMP='C:/Users/Pharaoh/AppData/Local/Temp' TEMP='C:/Users/Pharaoh/AppData/Local/Temp'
```

Always add `CXXFLAGS="-g -O2 -pipe" CFLAGS="-g -O2 -pipe"` to the `make`
invocations.

## 2. Dependencies

Install via pacman:

```sh
pacman -S --needed --noconfirm \
  mingw-w64-x86_64-toolchain mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-nettle mingw-w64-x86_64-gnutls \
  mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-gettext mingw-w64-x86_64-libidn2 \
  mingw-w64-x86_64-boost mingw-w64-x86_64-gmp mingw-w64-x86_64-argon2 \
  mingw-w64-x86_64-meson mingw-w64-x86_64-ninja
# For the 32-bit Explorer shell extension also:
pacman -S --needed --noconfirm mingw-w64-i686-gcc
```

Notes:
- **Boost.Regex >= 1.76 is required** (works around a GCC `std::regex` bug).
- Verified with gcc 16.1.
- If pacman complains about a partial upgrade / gcc-libs version pin, run a full
  `pacman -Syu` once to reconcile. Do **not** run a bare `pacman -Sy`.

## 3. libfilezilla 0.56.1 (>= 0.56.1, from source)

Download from <https://lib.filezilla-project.org/download.php>.

```sh
./configure --prefix=C:/msys64/mingw64 --disable-static
mingw32-make -j$(nproc)
```

The libtool `make install` is broken on this toolchain; **install manually**:

- `build/lib/.libs/libfilezilla-58.dll` -> `C:/msys64/mingw64/bin/`
- `libfilezilla.dll.a` + `.la` -> `C:/msys64/mingw64/lib/`
- headers tree `lib/libfilezilla/` -> `C:/msys64/mingw64/include/`
- generated `build/lib/libfilezilla/version.hpp` -> alongside the headers
- `build/lib/libfilezilla.pc` -> `C:/msys64/mingw64/lib/pkgconfig/`

## 4. fzssh 1.3.0 / libfzssh-client (>= 1.3.0, from source, meson)

Download from <https://fzssh.filezilla-project.org/>.

```sh
meson setup build --prefix=C:/msys64/mingw64 --buildtype=release
meson compile -C build
meson install -C build   # meson install works fine here
```

## 5. wxWidgets 3.3.3 (from source - REQUIRED for dark mode)

This tree uses the Windows appearance support in wxWidgets 3.3. Live manual
theme changes require the bundled patch for **3.3.3** described below. Build
3.3.3 into a **separate prefix** so it does not collide
with any system wx 3.2.x (keeps it reversible).

Source: `wxWidgets-3.3.3.tar.bz2` from the wxWidgets GitHub releases.

> **Apply both wxWidgets patches first.** With stock wxWidgets 3.3.x, owner-drawn
> checkboxes/radio buttons render blank in dark dialogs *in FileZilla*, because
> FileZilla gives many controls the same id (`nullID = wxID_HIGHEST`) and
> wxWidgets dispatches `WM_DRAWITEM` by id. Apply
> [`patches/wx333-darkmode-ownerdrawn-fixes.patch`](patches/wx333-darkmode-ownerdrawn-fixes.patch)
> (from this repo) to the extracted wxWidgets source before building:
>
> ```sh
> cd wxWidgets-3.3.3
> patch -p1 < /path/to/this/repo/patches/wx333-darkmode-ownerdrawn-fixes.patch
> patch -p1 < /path/to/this/repo/patches/wx333-runtime-appearance.patch
> ```
>
> It patches `src/msw/window.cpp` to route `WM_DRAWITEM` by control HWND instead
> of id. The second patch extends the 3.3.3 system-theme change path to manual
> Light/Dark/System changes with existing windows. Stock 3.3.3 still returns
> `CannotChange` for these calls. See the
> [upstream discussion](https://github.com/wxWidgets/wxWidgets/pull/26516).
> If you rebuild wxWidgets after editing either patch, run `mingw32-make` in
> `build-msw` and redeploy `wxmsw333u_core_gcc_custom.dll`.

```sh
mkdir build-msw && cd build-msw
../configure \
  --prefix=C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix \
  --enable-printfposparam --with-msw --disable-tests --without-opengl \
  --enable-shared MAKE=mingw32-make CXXFLAGS="-O2 -pipe"
mingw32-make -j$(nproc)
```

> Do **not** pass `--enable-unicode` - it was removed in wx 3.3 (always on) and
> passing it errors.

`make install` partially fails (Git-sh chokes on a multi-line header-install
`for` loop). Finish manually:

```sh
cp -r <wx-src>/include/wx  C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix/include/wx-3.3/
cp build-msw/lib/*.dll     C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix/lib/
```

(`wx-config`, `setup.h`, and the import libraries do install correctly. When
upgrading wxWidgets in an existing prefix, delete the old `include/wx-3.3/wx`
tree before copying the new headers, then do a clean FileZilla rebuild.)

Copy the wx 3.3 runtime DLLs next to where filezilla.exe will live so the app
self-resolves them:

```sh
cp build-msw/lib/wxmsw333u_*_gcc_custom.dll C:/msys64/mingw64/bin/
cp build-msw/lib/wxbase333u*_gcc_custom.dll C:/msys64/mingw64/bin/   # no underscore after 333u: also matches wxbase333u_gcc_custom.dll
```

### The wx-config wrapper

Create `C:/Users/Pharaoh/Downloads/fzbuild/bin/wx-config` so FileZilla's build
uses the 3.3 prefix unambiguously:

```sh
#!/bin/sh
exec C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix/bin/wx-config \
  --prefix=C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix \
  --exec-prefix=C:/Users/Pharaoh/Downloads/fzbuild/wx33-prefix "$@"
```

Make it executable (`chmod +x`). The environment in step 1 already puts
`fzbuild/bin` first on `PATH`.

## 6. FileZilla itself (this fork)

Build out-of-tree from a `compile/` subdirectory (kept out of git via
`.gitignore`):

```sh
mkdir -p compile && cd compile
../configure --prefix=C:/msys64/mingw64 --disable-static \
  --disable-manualupdatecheck --disable-dependency-tracking MAKE=mingw32-make \
  --with-wx-config=/c/Users/Pharaoh/Downloads/fzbuild/bin/wx-config \
  --with-pugixml=builtin
mingw32-make -j$(nproc) SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe' \
  CXXFLAGS="-g -O2 -pipe" CFLAGS="-g -O2 -pipe"
mingw32-make -C src/interface install SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'
```

Result: `C:/msys64/mingw64/bin/filezilla.exe`, resources in `share/filezilla/`,
config in `%APPDATA%\FileZilla`.

## 7. Translation catalogs (do not skip)

The out-of-tree build cannot find the shipped `.pot`, so copy it in first:

```sh
cd compile/locales
cp ../../locales/filezilla.pot ./filezilla.pot
mingw32-make -j$(nproc) allmo SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'
mingw32-make install SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe'
```

Installs 60 `.mo` files to `share/locale/<lang>/LC_MESSAGES/filezilla.mo`.
Verify by setting `<Setting name="Language Code">de</Setting>` in
`%APPDATA%\FileZilla\filezilla.xml`.

## 8. Explorer shell extension (required for release packaging, both architectures)

Remote drag-and-drop to the desktop and Explorer needs a registered shell
extension. Both DLLs must be included explicitly: they are loaded through COM
and are absent from the application's normal DLL import table. The rev6
release builder checks both DLLs and refuses to package a missing or incorrect
extension. Build them with static compiler runtimes so Explorer does not need
MinGW installed.

Build each architecture separately, invoking `configure` via a **relative path**
(so native make can stat the srcdir).

64-bit (from `compile/src/fzshellext/64`):

```sh
PATH=/c/msys64/mingw64/bin:/usr/bin:/bin \
../../../../src/fzshellext/configure \
  --prefix=C:/msys64/mingw64 --exec-prefix=C:/msys64/mingw64 \
  --host=x86_64-w64-mingw32 MAKE=mingw32-make
mingw32-make SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe' \
  CXXFLAGS="-O2 -pipe" LDFLAGS="-static-libgcc -static-libstdc++"
```

32-bit (from `compile/src/fzshellext/32`) - put mingw32 first on PATH so the
unprefixed `windres`/`dlltool` are i686; mingw64 supplies `mingw32-make`. Use
the static-libgcc/libstdc++ flags (but **not** bare `-static`, which makes
libtool build a static `.a` instead of the DLL):

```sh
PATH=/c/msys64/mingw32/bin:/c/msys64/mingw64/bin:/usr/bin:/bin \
../../../../src/fzshellext/configure \
  --prefix=C:/msys64/mingw64 --exec-prefix=C:/msys64/mingw64 \
  --host=i686-w64-mingw32 MAKE=mingw32-make
mingw32-make SHELL='C:/PROGRA~1/Git/usr/bin/sh.exe' \
  CXXFLAGS="-O2 -pipe" LDFLAGS="-static-libgcc -static-libstdc++"
```

> If a build dies with `No rule to make target .../Makefile.am`, the srcdir was
> passed in MSYS `/c/...` form that native make cannot stat - reconfigure via a
> relative path.

Deploy to the build prefix. The release installer performs registration on the
target machine. The manual registration commands below are only for testing a
development build; the DLL tries HKLM first and falls back to HKCU when needed:

```sh
cp 64/.libs/libfzshellext-0.dll C:/msys64/mingw64/bin/fzshellext_64.dll
cp 32/.libs/libfzshellext-0.dll C:/msys64/mingw64/bin/fzshellext.dll
"$WINDIR/System32/regsvr32" /s C:/msys64/mingw64/bin/fzshellext_64.dll
"$WINDIR/SysWOW64/regsvr32" /s C:/msys64/mingw64/bin/fzshellext.dll
```

CLSID `{DB70412E-EEC9-479C-BBA9-BE36BFDDA41B}`.

## 9. Launch

```
C:\msys64\mingw64\bin\filezilla.exe
```

(The wx 3.3 and dependency DLLs sit alongside it, so it self-resolves.) Toggle
the theme in *Settings > Interface > Appearance > Color theme*; it takes effect
after clicking **OK**, without restarting. The bundled patched wxWidgets core
DLL must be deployed alongside the rebuilt application.

## 10. Package a themed release

The themed release scripts now live in the repository. Use the PowerShell
builder instead of the old untracked portable-bundle and installer scripts.
It requires Windows PowerShell 5.1 or later, Git for Windows, and the MSYS2
MinGW64 NSIS, objdump, and strip tools.

From PowerShell, using the current checkout and build prefix:

```powershell
& 'C:\Users\Pharaoh\Downloads\FileZilla-Themed-For-Windows-rev6\packaging\windows\build-release.ps1' `
  -Prefix 'C:\msys64\mingw64' `
  -OutputRoot 'C:\Users\Pharaoh\Downloads\fzbuild\release' `
  -Revision 6
```

This packages the rebuilt application and runtime DLLs from the build prefix.
The optional `-ApplicationBundle` instead packages an already staged and
stripped application's exact bytes. For rev6, that bundle must contain the
rebuilt FileZilla executable and runtime-patched wxWidgets core DLL. Ensure
the source tree matches the application build being packaged.

Adjust the checkout location, prefix, output root, and revision for your own
build. Update the changelog and release notes before packaging. An existing
revision directory is never overwritten; move it aside before rebuilding.

The builder validates both shell-extension DLLs, resolves application runtime
dependencies, and creates the installer, installer ZIP, portable ZIP, build
information, and SHA-256 checksums. Commit the matching source, publish a release
tag, and upload the three downloads. Include their checksums in the release
description; GitHub generates source archives from the tag. Build information
and other local validation records do not need to be uploaded.

The **Shell Extension** component is selected by default. Registration mirrors
the extension's COM registration in both machine registry views. Upgrades can
defer replacing a DLL held by Explorer until reboot. Uninstall preserves a
registration owned by another installation and restores an earlier machine
registration when its DLL still exists. Current-user registrations are left
alone. Original FileZilla shares the same extension identifier, so changing
another installation may require repairing the themed installation.

Complete installer, live-theme, and drag-and-drop testing on Windows before
publishing. Open menus before switching themes and repeat changes during active
transfers. The portable ZIP includes the DLLs but does not
register them when extracted; use the installer for Explorer integration.

References: [NSIS library setup](https://nsis.sourceforge.io/Docs/AppendixB.html)
for replacement of locked DLLs, and [Windows registry views](https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys)
for the separate COM registrations and shared Directory copy-hook association.

## Rebuild gotchas

- **After editing `configure.ac`**, autotools maintainer-mode tries to re-run
  `aclocal`/`autoconf` and fails on this toolchain. Fix by normalizing
  timestamps: `touch -t 202001010000` the `configure.ac`/`*.am`/`m4/*.m4`
  sources, then `touch` the generated files (`aclocal.m4`, `configure`,
  `config.h.in`, every `Makefile.in`, then the build-tree `config.status`/
  `Makefile`/`config.h`) so the sources look older than the outputs.
- **When switching wx versions or changing vtable-affecting headers**, run
  `mingw32-make clean` first - make does not recompile on flag changes alone, so
  stale objects link against the new libraries and produce undefined-reference
  errors.
