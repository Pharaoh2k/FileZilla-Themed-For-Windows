# FileZilla 3.70.6 - Themed (Windows 64-bit) - rev6

Released by Pharaoh2k, 2026-09-30.

## Changes in rev6

- **Live theme switching:** choose Follow system setting, Dark, or Light in
  Settings > Interface > Appearance > Color theme, then click OK. The new
  theme applies immediately. Connections, active transfers and window state
  are retained; Cancel leaves the current theme unchanged.
- **Rendering fixes:** menus opened before a theme change, active transfer rows,
  tinted site tabs, message-log text, and open Filter/Search condition panels
  now refresh correctly. Group-box labels remain readable in both themes.
- **Desktop and Explorer drag-and-drop:** Setup now includes a checked Shell
  Extension component and both required extension DLLs. Earlier themed
  installers omitted them, preventing remote files and folders from being
  dragged onto the desktop or into Explorer without another registered copy.
- **Installer handling:** upgrades can replace extension DLLs held by Explorer
  after a restart. Uninstall preserves another installation's registration
  and restores an earlier machine registration when its DLL still exists.

Built with wxWidgets 3.3.3 and the bundled runtime-appearance patch. Previous
rev5 fixes, including the message-log tab startup assertion fix, are retained.
See CHANGELOG.md for release history.

## Downloads

- `FileZilla-Themed-3.70.6-win64-setup-rev6.exe` - installer.
- `FileZilla-Themed-3.70.6-win64-setup-rev6.zip` - installer ZIP with release notes.
- `FileZilla-Themed-3.70.6-win64-rev6.zip` - portable bundle.

GitHub provides source archives from the release tag. SHA-256 checksums for the
three downloads are included in the GitHub release description.

## Installation and compatibility

Close FileZilla, run Setup, and leave Shell Extension checked for desktop and
Explorer drag-and-drop. Complete any restart requested by Setup. Saved sites
and settings remain in the existing FileZilla profile.

The portable bundle includes the extension DLLs but does not register them.
Use the installer for desktop/Explorer integration. Original FileZilla and this
fork share the extension identifier; installing or removing another version
can change its registration. Re-running this installer with Shell Extension
selected repairs it.

This is an x64 Windows build. The installer is unsigned. License: GNU GPL v3
or, at your option, any later version.

## Validation

Pharaoh2k confirmed the corrected rev6 build works after installation testing.
Regression checks covered all menus and Settings pages, major dialogs, live
FTP transfers across repeated theme changes, FTPS/SFTP file integrity, site
tints, log retention, and open dialogs following Windows app-theme changes.
Testing was performed on Windows 11; it does not cover every server, Windows
version, or feature combination.
