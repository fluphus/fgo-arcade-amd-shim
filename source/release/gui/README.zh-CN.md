# FGO Arcade AMD 补丁 · 2026.09.25

[English](README.md) | [日本語](README.ja.md)

**补丁内置 60 FPS 限帧与显示同步。**

本次更新改进了 60 FPS 帧节奏，在兼容的绘制之间复用纹理和采样器状态，加速分块光照计算，并修复场景颜色复制后的渲染目标状态恢复。

## 显示设置

- 在 AMD Software 中，将「等待垂直刷新」设为 **「除非应用程序指定，否则关闭」**。显示同步由补丁控制，请勿强制设为「始终关闭」或「始终开启」。
- 关闭所有游戏叠加层，包括 RTSS / MSI Afterburner、AMD 性能指标、Steam、Discord 和 Xbox Game Bar。叠加层可能改变呈现模式，增加显示延迟。
- 关闭其他限帧设置，包括 RTSS 中针对 `ago.exe` 的限帧。仅隐藏浮窗不会关闭其限帧器。

## 安装与恢复

1. 完整解压压缩包，退出游戏。保持 `game-patch` 文件夹与 `FgoAmdPatch.exe` 位于同一目录。
2. 运行 `FgoAmdPatch.exe`，点击「浏览」，选择包含 `ago.exe` 的游戏目录，也可以选择其包含 `App` 文件夹的上一级目录。
3. 点击「安装 / 更新」，完成后从原来的启动器进入游戏。
4. 如需撤回安装，选择同一目录，点击「恢复上次备份」。备份保存在游戏目录的 `shim-backups` 文件夹中。

支持 Windows 10 / 11 64 位，使用系统自带的 .NET Framework 和 Windows PowerShell。安装器会备份原文件，保留账号、服务器地址、存档和启动器配置。本包不包含游戏本体。

源码、采样工具和夹具位于 [仓库](https://github.com/fluphus/fgo-arcade-amd-shim)，用法见 [开发说明](https://github.com/fluphus/fgo-arcade-amd-shim/blob/main/source/README.md)。`SHA256SUMS.txt` 用于校验文件。
