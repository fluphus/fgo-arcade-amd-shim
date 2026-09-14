# FGO Arcade AMD Patch · 2026.09.14

[中文](README.zh-CN.md) | [日本語](README.ja.md)

Only **AMD Radeon RX 7900 XTX** has been tested. The performance guarantee is limited to **60 FPS throughout PVP battles on that GPU**. Other GPUs, PVE and loading screens have no performance guarantee.

**Only 1920×1080 (1080p) is supported. Other resolutions have known rendering errors. The patch has a built-in 60 FPS cap.**

## Install and Restore

1. Extract the entire archive and close the game.
2. Run `FgoAmdPatch.exe`. Click “浏览” (Browse) and select the folder containing `ago.exe`, or its parent containing `App`.
3. Click “安装 / 更新” (Install / Update), then use your usual game launcher.
4. To undo the installation, select the same folder and click “恢复上次备份” (Restore Last Backup). Backups are in the game's `shim-backups` folder.

Requires 64-bit Windows 10/11 with its included .NET Framework and Windows PowerShell. The installer backs up existing files and preserves account, server, save and launcher settings. Game files are not included.

See [Developer Instructions](source/README.md) for source, capture tools and fixtures. `SHA256SUMS.txt` provides file checksums.
