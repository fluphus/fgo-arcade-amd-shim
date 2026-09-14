# Source and Developer Tools

This directory contains source for the renderer, installer and development tools.
Run commands from this `source` directory in 64-bit Windows PowerShell.

## Build

Install [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases) and add its
`bin` directory to PATH (`x86_64-w64-mingw32-gcc.exe`, `llvm-nm.exe`). Python tools
need 64-bit Python 3.10+ and `pefile`. GUI builds use Windows' .NET Framework C# compiler.

```powershell
python -m pip install -r .\tools\requirements.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\release\gui\build.ps1
```

Renderer output: `build/opengl32.dll` and `build/amdcfg/amdOglpSettings.cfg`.
The GUI output is `build/gui-release/FgoAmdPatch.exe`. It uses `game-patch` and
`release.json` in the release root.

## Capture and Stop

The native sampler is prebuilt in `tools/bin`. Rebuild it when needed with
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build-tools.ps1`.
Start the game before attaching. Enter the folder containing `ago.exe`:

```powershell
$gameDirectory = Read-Host 'Game directory'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\start-capture.ps1 -GameDirectory $gameDirectory -Seconds 600
```

The command prints `CAPTURE=<directory>`. Default output is
`captures/battle_<timestamp>`. Data is written continuously. Keep each capture
directory intact for analysis. Stop early using the printed directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\stop-capture.ps1 -CaptureDirectory '.\captures\battle_<timestamp>'
python .\tools\analyze-capture.py '.\captures\battle_<timestamp>'
```

Manual stop retains data and lets an active eight-second stack burst finish;
do not force-kill the native sampler. The stop script does not stop the game.
An elevated game requires an equally elevated capture shell. Attach after the
renderer loads. This tool expects this shim's symbols and counters, not an
unmodified NVIDIA game DLL.

`analysis.json` contains interval summaries and symbolized stacks;
`all_sub60_bins.csv` includes every estimated sub-60 bin. Values near 59.8-59.9
may reflect polling precision. CPU stack samples are triggered by sustained
low FPS; short transitions may be missed. Sampling adds overhead and does not
measure GPU duration or record video. Record battle/death times to distinguish
gameplay from loading and menus.

## Fixtures

CPU-only pointer/header regressions use the included small shader fixtures:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-fixtures.ps1
```

With an AMD OpenGL 4.5 context, also exercise ordinary sampler uploads, handle
lifetime, changed mapped bytes, rendering/state restoration and 2000 draws:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-fixtures.ps1 -Driver
```

Add `-Benchmarks` for the SSE4.1 mapped-memory read benchmark.
Results and compiler logs go to `build/release-fixtures`. Microbenchmarks are
not game-FPS measurements. The bundled regression inputs are selected captured
shader text, not a complete battle replay.
