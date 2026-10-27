# FileZilla 3.70.6 - Themed (Windows 64-bit) - rev7

Released by Pharaoh2k, 2026-10-27.

## Changes in rev7

- **Notification bubble tray icon:** with Action after queue completion > Show
  notification bubble (the default, in the transfer queue's context menu), the
  system tray now shows the FileZilla icon instead of a blank space when the
  queue finishes. With wxWidgets 3.3, only the 16x16 image of the application
  icon was loaded, so the notification's tray icon was created without an
  image and the main window's large icon was 16x16. All icon sizes are now
  loaded.
- **Notification sender:** Setup now marks the Start Menu and desktop shortcuts
  with FileZilla's application ID, as the official installer does. Windows then
  shows the "Transfers finished" notification under FileZilla Themed, with its
  icon, and lists the app in Settings > System > Notifications. Previously it
  appeared under an anonymous "Microsoft.Explorer.Notification" sender.

Only filezilla.exe has been rebuilt. The wxWidgets 3.3.3 libraries, shell
extensions and other DLLs are unchanged from rev6, and all rev6 changes are
retained. See CHANGELOG.md for release history.

## Downloads

- `FileZilla-Themed-3.70.6-win64-setup-rev7.exe` - installer.
- `FileZilla-Themed-3.70.6-win64-setup-rev7.zip` - installer ZIP with release notes.
- `FileZilla-Themed-3.70.6-win64-rev7.zip` - portable bundle.

GitHub provides source archives from the release tag. SHA-256 checksums for the
three downloads are included in the GitHub release description.

## Installation and compatibility

Close FileZilla, run Setup, and leave Shell Extension checked for desktop and
Explorer drag-and-drop. Complete any restart requested by Setup. Saved sites
and settings remain in the existing FileZilla profile. Running Setup over an
earlier revision recreates the Start Menu shortcut with the application ID.

The portable bundle creates no shortcut, so Windows shows its notifications
under an anonymous sender; the tray icon fix applies to both downloads. The
portable bundle includes the extension DLLs but does not register them. Use the
installer for desktop/Explorer integration and named notifications. Original
FileZilla and this fork share the extension identifier; installing or removing
another version can change its registration. Re-running this installer with
Shell Extension selected repairs it.

This is an x64 Windows build. The installer is unsigned. License: GNU GPL v3
or, at your option, any later version.

## Validation

A tester confirmed that a build with the same filezilla.exe resolves the blank
tray icon and the missing notification. Local tests on Windows 11 ran FTP
transfers with the notification enabled and checked the tray icon, the window
icon sizes at each display scale, and the notification sender with and without
the application ID shortcut. This does not cover every Windows version or
notification setting.
