# FGO Arcade AMD 补丁 · 2026.09.22

[English](README.en.md) | [日本語](README.ja.md)

**补丁内置锁定 60 FPS。**

本次改进 60 FPS 限帧器，避免计时误差逐帧累积。包含此前的渲染修复。

## 显示设置

请关闭额外的限帧设置，例如 RTSS 中针对 `ago.exe` 的限帧，避免与内置 60 FPS 限帧叠加。

如果发现画面撕裂，可以在 AMD Software: Adrenalin Edition 中尝试开启 **Radeon 增强同步（Enhanced Sync）**。如果普通垂直同步导致掉帧，可尝试将「等待垂直刷新」设为「始终关闭」，同时保留增强同步。效果可能因显卡和驱动版本而异。

## 安装与恢复

1. 完整解压压缩包，退出游戏。
2. 运行 `FgoAmdPatch.exe`，点击「浏览」，选择包含 `ago.exe` 的游戏目录，也可以选择其包含 `App` 文件夹的上一级目录。
3. 点击「安装 / 更新」，完成后从原来的启动器进入游戏。
4. 如需撤回安装，选择同一目录，点击「恢复上次备份」。备份保存在游戏目录的 `shim-backups` 文件夹中。

支持 Windows 10 / 11 64 位，使用系统自带的 .NET Framework 和 Windows PowerShell。安装器会备份原文件，保留账号、服务器地址、存档和启动器配置。本包不包含游戏本体。

源码、采样工具和夹具用法见 [开发说明](source/README.md)。`SHA256SUMS.txt` 用于校验文件。
