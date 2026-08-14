# FGO Arcade AMD 渲染补丁 (amdshim)

让 Fate/Grand Order Arcade（SEGA ALLS UX，OpenGL 4.x + NVIDIA 专属扩展）在
AMD Radeon 显卡上正常渲染的 OpenGL 兼容层。已验证机型：RX 7900 XTX（RDNA3），
Windows 11，游戏 11.50/11.51。

## 安装 / Install / インストール

中文：打开 `build` 文件夹，把 `opengl32.dll` 和 `opengl32real.dll` 复制到游戏根目录（`ago.exe` 旁边）。
不要放进 `C:\Windows\System32`，也不要覆盖系统文件；segatools / fgohook / amdaemon / hexedit 等其它配置保持不变。

English: Copy `build\opengl32.dll` and `build\opengl32real.dll` into your game root (next to `ago.exe`).
Do not put them in `C:\Windows\System32` or overwrite system files. Leave your segatools / fgohook / amdaemon / hexedit setup unchanged.

日本語：`build` フォルダ内の `opengl32.dll` と `opengl32real.dll` を、ゲームのルート（`ago.exe` の隣）にコピーしてください。
`C:\Windows\System32` には入れず、システムファイルは上書きしないでください。segatools / fgohook / amdaemon / hexedit などの設定は変更しないでください。

## 文件

- `build\opengl32.dll` —— 兼容层本体（复制到游戏目录；原客户端本来没有该文件，
  放进去即可被游戏优先加载）。
- `build\opengl32real.dll` —— 系统 OpenGL 客户驱动的副本（opengl32.dll 的导出会转发到它）。
  注意：本包内的这份副本取自 Windows 11 26100。若你的 Windows 版本不同，
  请删除它并把你自己的 `C:\Windows\System32\opengl32.dll` 复制一份、改名为
  `opengl32real.dll` 放进游戏目录，否则可能出现转发不匹配。
- `shim.c` / `shim.def` —— 源码（可自行重建，见下文）。

## 校验

SHA256：

- `opengl32.dll` `BD047809DEBE02AD297BB34D4DB8C5408A54663A13E74EF20C0729F41D379B0D`
- `opengl32real.dll` `35168907D9ACFA3D6B10C1B546F433F8269EAF1C4200DB85C7D1142E0F3DB9CD`
- `shim.c` `733E1331C5499564AA52D9AE66360550E52915EF18CA9925F909484AC4C2CBE7`
- `shim.def` `89718FFE776AF5A70D0F7F05A55A08A8CA804E45D3E9644A6BBD201CE21653E9`

## 它做了什么

- `wglSwapBuffers` 走 `opengl32real.dll` 实例（双实例混用会导致 swap 恒失败、白屏）。
- 把 NVIDIA 专用 shader 改写为 AMD 可编译的形式：`NV_shader_buffer_load` 指针 →
  SSBO 索引、光照/粒子模拟 pass 的指针 uniform → SSBO、bindless 纹理句柄 → ARB 等价物。
- 模拟 `NV_vertex_buffer_unified_memory`：顶点属性 + 索引缓冲（element array）按绘制
  时刻重新绑定，修正 stride/relativeoffset。
- 补齐游戏实际使用的绘制入口包装：`glMultiDrawElementsBaseVertex`、
  `glDrawElementsBaseVertex`、`glMultiDrawArrays`、BaseInstance 变体等。

## 诊断

默认静默。需要详细日志（`glcalls.log`、`swap.log`、`shim.log`、shader 转储等）时，
启用方式见 `shim.c` 中的 `g_log_on` 开关。

## 重建

```powershell
x86_64-w64-mingw32-gcc -O2 -shared -o build\opengl32.dll shim.c shim.def -lgdi32 -luser32
```

需要 MinGW-w64（x86_64）工具链，`x86_64-w64-mingw32-gcc` 需在 PATH 中。

## 兼容性说明

shader 改写按模式匹配，不绑定具体版本号，11.00/11.50/11.51 都应可用；已实测 11.51。
粒子、光照剔除、轮廓线、MLAA、色调映射、UI 文字均正常。若某版本出现新增的
NV 指针写法，按 `shim.c` 里 `rewrite_pointer_shaders` / `rewrite_light_pointers` /
`rewrite_particle_nv_arith` 的模式追加即可。

注：游戏内影片为 MP4，由系统 Media Foundation/D3D9 解码，与本补丁无关。
