# FileZilla Themed for Windows changelog

## 3.70.6 rev6 - 2026-09-30

- Refresh Windows popup-menu theme caches when switching appearance, fixing
  unreadable text and mismatched gutters after dark menus have been opened.
- Repaint cached transfer-progress rows and preserve tinted-tab colours across
  theme changes.
- Refresh filter/search condition backgrounds when an open dialog follows a
  Windows app-theme change.
- Apply Light, Dark, or Follow system setting when clicking **OK** in Settings,
  without restarting FileZilla or recreating its windows.
- Extend wxWidgets 3.3.3 system-theme handling to support manual runtime
  appearance changes using a bundled source patch.
- Refresh native controls, file-list colours, site tints, and message-log
  colours during theme changes; keep group-box labels readable after a
  light-mode startup.
- Restore dragging remote files and folders onto the Windows desktop and into
  Explorer by including the 32-bit and 64-bit FileZilla shell extensions.
- Add a default-selected **Shell Extension** installer component and register
  both architectures. Earlier themed installers omitted the DLLs and their
  registration, so the desktop could not accept remote drags.
- Handle extension DLLs locked by Explorer during upgrades and removal; Setup
  requests a restart when replacement must finish after reboot.
- Preserve shell-extension registrations taken over by another installation.
  When possible, uninstall restores the previous machine registration.
- Track the release builder and installer in the repository. Packaging checks
  extension presence, architecture, exports, and dependencies before building.
- Include license documents and release notes in the downloads; publish SHA-256
  checksums in the release description. Source archives come from the release tag.

Remote drag-and-drop through Explorer requires the registered extension;
extracting the portable ZIP alone does not register it. The shell-extension
fix was verified by installation and drag-and-drop testing. The updated rev6
also rebuilds FileZilla and the wxWidgets core DLL for live theme switching.
The final build passed maintainer testing after the rendering fixes.

## 3.70.6 rev5 - 2026-09-04

- Fix the startup assertion and missing Message log tab when the log is shown
  as a tab in the transfer queue pane under wxWidgets 3.3.3.

## 3.70.6 rev4 - 2026-09-03

- Upgrade to wxWidgets 3.3.3 and restore readable group-box labels in dark mode.
- Fix the local file list and tree going blank behind Windows shell dialogs.

## Earlier revisions

- Add native Windows dark mode and the Color theme preference.
- Port FileZilla 3.70.6 to wxWidgets 3.3.
- Fix blank or mislabeled owner-drawn controls in dark dialogs.
- Fix file-list flicker on mouse hover.
