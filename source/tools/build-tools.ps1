$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cc = (Get-Command x86_64-w64-mingw32-gcc.exe -ErrorAction Stop).Source
$output = Join-Path $PSScriptRoot 'bin\battle_cpu_sample.exe'
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
& $cc -O2 (Join-Path $root 'tests\battle_cpu_sample.c') -o $output -ldbghelp
if ($LASTEXITCODE -ne 0) { throw 'CPU sampler build failed.' }
Get-FileHash -LiteralPath $output -Algorithm SHA256
