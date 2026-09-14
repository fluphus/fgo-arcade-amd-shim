[CmdletBinding()]
param(
    [string]$Output = "build/opengl32.dll"
)

$ErrorActionPreference = "Stop"

$cc = Get-Command x86_64-w64-mingw32-gcc.exe -ErrorAction SilentlyContinue
if (-not $cc) {
    throw "x86_64-w64-mingw32-gcc.exe was not found on PATH"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outPath = Join-Path $root $Output
$outDir = Split-Path -Parent $outPath
New-Item -ItemType Directory -Force $outDir | Out-Null

$source = Join-Path $root "shim.c"
$definition = Join-Path $root "shim.def"

$previousErrorAction = $ErrorActionPreference
try {
    # Windows PowerShell 5.1 turns redirected native warnings into error records.
    $ErrorActionPreference = 'Continue'
    & $cc.Source -O2 -shared -o $outPath $source $definition -lgdi32 -luser32
    $buildExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
}
if ($buildExitCode -ne 0) {
    throw "shim build failed with exit code $buildExitCode"
}

Get-FileHash -Algorithm SHA256 $outPath
$config = Join-Path $outDir 'amdcfg'
New-Item -ItemType Directory -Force $config | Out-Null
[IO.File]::WriteAllText((Join-Path $config 'amdOglpSettings.cfg'),
    "#2005695891, 8192`r`n#3582369058, 1024`r`n", [Text.Encoding]::ASCII)
