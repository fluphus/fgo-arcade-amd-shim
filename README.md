# FGO Arcade AMD Patch · 2026.09.16

[中文](README.zh-CN.md) | [日本語](README.ja.md)

Only **AMD Radeon RX 7900 XTX** has been tested. In the latest PVP test, performance stayed at **60 FPS for almost the entire match**, with brief dips to about **57 FPS when switching servants**. Other GPUs, PVE and loading screens have no performance guarantee.

**Only 1920×1080 (1080p) is supported. Other resolutions have known rendering errors. The patch has a built-in 60 FPS cap.**

This update fixes stretched, multicolored castle-wall textures on an additional map. It also includes the earlier ranged-attack trail, London white-flash and shader recovery fixes.

## Install and Restore

1. Extract the entire archive and close the game.
2. Run `FgoAmdPatch.exe`. Click “浏览” (Browse) and select the folder containing `ago.exe`, or its parent containing `App`.
3. Click “安装 / 更新” (Install / Update), then use your usual game launcher.
4. To undo the installation, select the same folder and click “恢复上次备份” (Restore Last Backup). Backups are in the game's `shim-backups` folder.

Requires 64-bit Windows 10/11 with its included .NET Framework and Windows PowerShell. The installer backs up existing files and preserves account, server, save and launcher settings. Game files are not included.

See [Developer Instructions](source/README.md) for source, capture tools and fixtures. `SHA256SUMS.txt` provides file checksums.
