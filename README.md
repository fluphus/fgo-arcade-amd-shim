# FGO Arcade AMD Patch · 2026.09.23

[中文](README.zh-CN.md) | [日本語](README.ja.md)

**The patch has a built-in 60 FPS cap.**

Screen tearing is removed by one opaque pixel at the bottom-right, without turning VSync on.

## Display Settings

Disable additional frame-rate limits, such as an RTSS profile for `ago.exe`, so they do not stack with the built-in 60 FPS cap. Leave **Wait for Vertical Refresh** off.

## Install and Restore

1. Extract the entire archive and close the game.
2. Run `FgoAmdPatch.exe`. Click **Browse** and select the folder containing `ago.exe`, or its parent containing `App`.
3. Click **Install / Update**, then use your usual game launcher.
4. To undo the installation, select the same folder and click **Restore Last Backup**. Backups are in the game's `shim-backups` folder.

Requires 64-bit Windows 10/11 with its included .NET Framework and Windows PowerShell. The installer backs up existing files and preserves account, server, save and launcher settings. Game files are not included.

See [Developer Instructions](source/README.md) for source, capture tools and fixtures. `SHA256SUMS.txt` provides file checksums.
