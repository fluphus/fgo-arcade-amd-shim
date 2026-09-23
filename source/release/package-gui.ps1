[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$BuildDirectory = 'build\gui-release',
    [string]$ReferenceRenderer = '..\game-patch\opengl32.dll'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$package = [IO.Path]::GetFullPath($Destination)
$zip = $package + '.zip'
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $zip)) { throw 'Choose a new release destination; previous releases are preserved.' }
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$build = Join-Path $root $BuildDirectory
New-Item -ItemType Directory -Path $build -Force | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
Push-Location $root
try {
    $sourceFiles = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'source-files.json') -Raw | ConvertFrom-Json

    $linkerName = Split-Path -Leaf $ReferenceRenderer
    & (Join-Path $root 'build.ps1') -Output (Join-Path $BuildDirectory $linkerName) *> (Join-Path $build 'renderer-build.log')
    & (Join-Path $PSScriptRoot 'gui\build.ps1') -Output (Join-Path $build 'FgoAmdPatch.exe') *> (Join-Path $build 'gui-build.log')
    $dll = Join-Path $build $linkerName
    $reference = if ([IO.Path]::IsPathRooted($ReferenceRenderer)) { $ReferenceRenderer } else { Join-Path $root $ReferenceRenderer }
    if (-not (Test-Path -LiteralPath $reference -PathType Leaf)) {
        throw 'Reference renderer DLL was not found.'
    }
    & python (Join-Path $root 'tests\release_binary_compare.py') $reference $dll
    if ($LASTEXITCODE -ne 0) { throw 'The release renderer differs from the deployed source build.' }

    New-Item -ItemType Directory -Path (Join-Path $package 'game-patch'), (Join-Path $package 'source') | Out-Null
    Copy-Item -LiteralPath $reference -Destination (Join-Path $package 'game-patch\opengl32.dll')
    Copy-Item -LiteralPath (Join-Path $build 'amdcfg') -Destination (Join-Path $package 'game-patch\amdcfg') -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.ps1') -Destination (Join-Path $package 'game-patch\install.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'gui\gui-action.ps1') -Destination (Join-Path $package 'game-patch\gui-action.ps1')
    Copy-Item -LiteralPath (Join-Path $build 'FgoAmdPatch.exe') -Destination (Join-Path $package 'FgoAmdPatch.exe')
    foreach ($name in @('README.md','README.zh-CN.md','README.ja.md')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "gui\$name") -Destination (Join-Path $package $name)
    }
    foreach ($name in $sourceFiles) {
        $target = Join-Path $package ('source\' + $name)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $target
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'gui\SOURCE.md') -Destination (Join-Path $package 'source\README.md')
    & (Join-Path $package 'source\tools\build-tools.ps1') *> (Join-Path $build 'sampler-build.log')
    function Write-Checksums {
        $hashLines = @(
            foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName) {
                $relative = $file.FullName.Substring($package.Length + 1).Replace('\', '/')
                if ($relative -eq 'SHA256SUMS.txt') { continue }
                '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash, $relative
            }
        )
        [IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'), [string[]]$hashLines, $utf8)
    }
    Write-Checksums
    $compiler = Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'csc.exe'
    $testSource = Join-Path $root 'tests\gui_release_test.cs'
    $testExe = Join-Path $build 'GuiReleaseTest.exe'
    $stub = Join-Path $build 'ago-stub.exe'
    & $compiler /nologo /target:exe /platform:x64 /codepage:65001 "/out:$testExe" `
        "/reference:$(Join-Path $build 'FgoAmdPatch.exe')" /reference:System.Windows.Forms.dll `
        /reference:System.Drawing.dll /reference:System.Web.Extensions.dll $testSource
    if ($LASTEXITCODE -ne 0) { throw 'GUI integration test compilation failed.' }
    & $compiler /nologo /target:winexe /platform:x64 /define:GAME_STUB "/out:$stub" $testSource
    if ($LASTEXITCODE -ne 0) { throw 'Test fixture compilation failed.' }
    $testRun = Join-Path $build ('install-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    & $testExe $package $testRun $stub
    if ($LASTEXITCODE -ne 0) { throw "GUI integration tests failed. Evidence: $testRun" }
    $rebuildDirectory = Join-Path $build 'packaged-source'
    New-Item -ItemType Directory -Path $rebuildDirectory -Force | Out-Null
    $rebuilt = Join-Path $rebuildDirectory $linkerName
    $cc = Get-Command x86_64-w64-mingw32-gcc.exe -ErrorAction Stop
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $cc.Source -O2 -shared (Join-Path $package 'source\shim.c') (Join-Path $package 'source\shim.def') -o $rebuilt -lgdi32 -luser32 *> (Join-Path $build 'packaged-source-build.log')
        $compileExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousErrorAction }
    if ($compileExit -ne 0) { throw 'Packaged renderer source failed to rebuild.' }
    & python (Join-Path $root 'tests\release_binary_compare.py') $reference $rebuilt
    if ($LASTEXITCODE -ne 0) { throw 'Packaged renderer source differs from the release DLL.' }
    & (Join-Path $package 'source\tools\run-fixtures.ps1') -Driver -Benchmarks *> (Join-Path $build 'packaged-fixtures.log')
    # Fixture output is build evidence, not part of the user-facing package.
    $generatedBuild = [IO.Path]::GetFullPath((Join-Path $package 'source\build'))
    if ($generatedBuild -ne ($package + '\source\build')) { throw 'Unexpected fixture output directory.' }
    Remove-Item -LiteralPath $generatedBuild -Recurse -Force
    Write-Checksums
    Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
    $zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    [IO.File]::WriteAllText(($zip + '.sha256'), ($zipHash + '  ' + (Split-Path -Leaf $zip) + "`r`n"), $utf8)
    [pscustomobject]@{ Package = $package; Zip = $zip; ZipSHA256 = $zipHash }
} finally { Pop-Location }
