# FGO Arcade AMD Patch · 2026.09.25

[中文](README.zh-CN.md) | [日本語](README.ja.md)

**The patch has a built-in 60 FPS cap and presentation synchronization.**

This update improves 60 FPS pacing, reuses texture and sampler state between compatible draws, and speeds up tiled lighting. It also fixes render-target state restoration during scene-color copies.

## Display Settings

- In AMD Software, set **Wait for Vertical Refresh** to **Off, Unless Application Specifies**. The patch controls presentation synchronization; do not force **Always Off** or **Always On**.
- Disable all in-game overlays, including RTSS / MSI Afterburner, AMD metrics, Steam, Discord and Xbox Game Bar. Overlays can change the presentation mode and add display latency.
- Disable additional frame-rate limits, including any RTSS profile for `ago.exe`. Hiding an overlay does not disable its frame limiter.

## Install and Restore

1. Extract the entire archive and close the game. Keep `game-patch` beside `FgoAmdPatch.exe`.
2. Run `FgoAmdPatch.exe`. Click **Browse** and select the folder containing `ago.exe`, or its parent containing `App`.
3. Click **Install / Update**, then use your usual game launcher.
4. To undo the installation, select the same folder and click **Restore Last Backup**. Backups are in the game's `shim-backups` folder.

Requires 64-bit Windows 10/11 with its included .NET Framework and Windows PowerShell. The installer backs up existing files and preserves account, server, save and launcher settings. Game files are not included.

Source, capture tools and fixtures are available in the [repository](https://github.com/fluphus/fgo-arcade-amd-shim). See the [developer instructions](https://github.com/fluphus/fgo-arcade-amd-shim/blob/main/source/README.md). `SHA256SUMS.txt` provides file checksums.
