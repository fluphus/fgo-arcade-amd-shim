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

The mapped sampler cache regression can be run directly when validating the
current renderer source. It checks publication boundaries, GPU-exposed maps,
buffer writes, unmap/remap and coherent mappings on an AMD OpenGL context:

```powershell
$out = '.\build\mapped-cache-driver'
New-Item -ItemType Directory -Force $out | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\mapped_read_cache_wrapper.c -o "$out\wrapper.dll" -lgdi32 -luser32
python .\tests\mapped_read_cache_driver_test.py "$out"
```

The production build does not enable the lifetime audit probe. The cache
counters are available to the optional sampler for diagnosing hit and miss
rates; they are not required for normal installation.

## London Fog Compute Regression

The fog compute shader uses translated NVIDIA pointers for its light data.
The dispatcher now refreshes those SSBO bindings from the current UBO before
execution. This prevents a previous draw's light range from leaking into fog.

Run the captured shader on an AMD OpenGL context:

```powershell
$out = '.\build\compute-pointer'
New-Item -ItemType Directory -Force $out | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\compute_pointer_wrapper.c -o "$out\wrapper.dll" -lgdi32 -luser32
python .\tests\compute_pointer_driver_test.py "$out\wrapper.dll" .\tests\fixtures\compute\london_scattering.comp "$out\result.json"
```

The test requires NumPy, included in `tools/requirements.txt`. It checks stale
SSBO bindings, UBO range changes and pointer writes with caches enabled and
disabled, plus a compute shader that uses no translated pointers. Expected
output has `all_fixed: true`; a failure exits with an error. This is a controlled
shader regression, not a complete battle replay. Results from the previous and
fixed implementations are in `../verification/compute-pointer-*.json`.

## Trail Vertex Binding and Shader Recovery

The trail regression uses a small captured pair of vertex buffers. It checks
the shared position/UV binding, cached replay, relocated NV addresses and
rejection of unrelated layouts. The old route reproduces nonfinite UV values;
the corrected route reproduces all 374 requested vertices exactly.

```powershell
$out = '.\build\trail-vertex'
New-Item -ItemType Directory -Force $out | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\trail_vertex_wrapper.c -o "$out\wrapper.dll" -lgdi32 -luser32
python .\tests\trail_vertex_driver_test.py "$out\wrapper.dll" .\tests\fixtures\trail "$out"
```

The texcoord regression exercises actual shader linking. Successful optional
adjustments retain their source and expected pixels. Rejected adjustments must
restore the original shader source and compilation state, so the shader remains
usable by compatible programs. It covers 13 cases, including swizzles, indexing,
macros and inactive code.

```powershell
$out = '.\build\texcoord-rollback'
New-Item -ItemType Directory -Force $out | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\texcoord_link_wrapper.c -o "$out\wrapper.dll" -lgdi32 -luser32
python .\tests\texcoord_link_driver_test.py --wrapper "$out\wrapper.dll" --output "$out\result.json"
```

Both tests use an AMD OpenGL context and the existing Python dependencies.
Results are included in `../verification/trail-vertex.json` and
`../verification/texcoord-rollback.json`. Restoring a rejected shader adjustment
does not fix a genuine vertex/fragment interface mismatch or establish support
for other game-client versions.

## Castle Background Vertex Layout

The castle regression checks the 32-byte background layout with two UV sets
and vertex color. A captured 1,326-vertex prefix reproduces the old 12-byte
UV/color displacement, then checks corrected, cached and relocated bindings.
Position, normal and tangent outputs must remain identical in every case.

```powershell
$out = '.\build\castle-vertex'
New-Item -ItemType Directory -Force $out | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\castle_vertex_wrapper.c -o "$out\wrapper.dll" -lgdi32 -luser32
python .\tests\castle_vertex_driver_test.py "$out\wrapper.dll" .\tests\fixtures\castle "$out\result.json"
```

The fixture includes only the two relevant vertex prefixes, not a full scene.
It uses the same AMD OpenGL context and Python dependencies as the trail test.
Expected results are in `../verification/castle-vertex.json`; all corrected
cases have zero incorrect UV and color vertices.

## Reuse an Existing Shader Cache

Revision 2 shares translated shaders across object IDs. To reuse a previous
installation's revision-1 cache without repeating translation:

```powershell
$gameDirectory = Read-Host 'Game directory'
python .\tools\migrate-shader-cache.py --source "$gameDirectory\shader-cache-r1" --destination "$gameDirectory\shader-cache-r2"
```

The original cache is retained. Migration is optional; the game creates the
new cache automatically when needed.

## Cache and Pacing Regression

The following checks historical cache payloads, the included shader fixtures,
draw state and limiter timing in a separate process. It does not attach to the
game. It requires a revision-1 cache from an earlier installation.

```powershell
$gameDirectory = Read-Host 'Game directory'
$testDirectory = '.\build\cache-pacing'
New-Item -ItemType Directory -Force $testDirectory | Out-Null
x86_64-w64-mingw32-gcc.exe -O2 -shared .\tests\external_optimization_wrapper.c -o "$testDirectory\wrapper.dll" -lgdi32 -luser32
python .\tools\migrate-shader-cache.py --source "$gameDirectory\shader-cache-r1" --destination "$testDirectory\migrated"
python .\tests\external_optimization_test.py --wrapper "$testDirectory\wrapper.dll" --old-cache "$gameDirectory\shader-cache-r1" --migrated-cache "$testDirectory\migrated" --output "$testDirectory\results.json"
```
