[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$ReleaseId = '20260915-mapped-sampler-cache',
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
    $revision = '838e5710a3a54aa947bf8867ce878c96274709c0'
    $sourceFiles = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'source-files.json') -Raw | ConvertFrom-Json

    $linkerName = Split-Path -Leaf $ReferenceRenderer
    & (Join-Path $root 'build.ps1') -Output (Join-Path $BuildDirectory $linkerName) *> (Join-Path $build 'renderer-build.log')
    & (Join-Path $PSScriptRoot 'gui\build.ps1') -Output (Join-Path $build 'FgoAmdPatch.exe') *> (Join-Path $build 'gui-build.log')
    $dll = Join-Path $build $linkerName
    $reference = if ([IO.Path]::IsPathRooted($ReferenceRenderer)) { $ReferenceRenderer } else { Join-Path $root $ReferenceRenderer }
    if ((Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash -ne '71D76756227EBDCA1BB38BB5A816193D774D75E3925D45E17E71297D1D3298CA') {
        throw 'Reference is not the user-confirmed baseline DLL.'
    }
    & python (Join-Path $root 'tests\release_binary_compare.py') $reference $dll
    if ($LASTEXITCODE -ne 0) { throw 'The release renderer differs from the deployed source build.' }

    New-Item -ItemType Directory -Path (Join-Path $package 'game-patch'), (Join-Path $package 'source'), (Join-Path $package 'verification') | Out-Null
    Copy-Item -LiteralPath $reference -Destination (Join-Path $package 'game-patch\opengl32.dll')
    Copy-Item -LiteralPath (Join-Path $build 'amdcfg') -Destination (Join-Path $package 'game-patch\amdcfg') -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.ps1') -Destination (Join-Path $package 'game-patch\install.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'gui\gui-action.ps1') -Destination (Join-Path $package 'game-patch\gui-action.ps1')
    Copy-Item -LiteralPath (Join-Path $build 'FgoAmdPatch.exe') -Destination (Join-Path $package 'FgoAmdPatch.exe')
    foreach ($language in @('zh-CN','en','ja')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "gui\README.$language.md") -Destination (Join-Path $package "README.$language.md")
    }
    Copy-Item -LiteralPath (Join-Path $package 'README.en.md') -Destination (Join-Path $package 'README.md')
    foreach ($name in $sourceFiles) {
        $target = Join-Path $package ('source\' + $name)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $target
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'gui\SOURCE.md') -Destination (Join-Path $package 'source\README.md')
    & (Join-Path $package 'source\tools\build-tools.ps1') *> (Join-Path $build 'sampler-build.log')
    $compilerVersion = (& x86_64-w64-mingw32-gcc.exe --version | Select-Object -First 1)

    function Write-Manifest {
        $files = @(
            foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName) {
                $relative = $file.FullName.Substring($package.Length + 1).Replace('\', '/')
                if ($relative -in @('release.json', 'SHA256SUMS.txt')) { continue }
                [ordered]@{ path = $relative; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
            }
        )
        $manifest = [ordered]@{
            schema = 1; release_id = $ReleaseId; baseline = $ReleaseId; source_revision = $revision
            renderer_commit = $revision
            architecture = 'AMD64'; pacing_hz = 60; configuration = 'embedded'
            supported_resolution = @(1920,1080); other_resolutions = 'Known rendering errors'
            tested_gpu = 'AMD Radeon RX 7900 XTX'; performance_scope = 'User reports almost entirely 60 FPS in PVP, briefly about 57 FPS on servant switches; other GPUs and modes unverified'
            compiler = $compilerVersion; source_inventory = 'source/release/source-files.json'
            created_at = [DateTimeOffset]::Now.ToString('o')
            renderer_reference_sha256 = (Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash
            build_command = "x86_64-w64-mingw32-gcc.exe -O2 -shared -o $linkerName shim.c shim.def -lgdi32 -luser32"
            packaged_dll_name = 'game-patch/opengl32.dll'
            visual_status = 'Baseline rendering previously confirmed; this update passed offline sampler pixel, state and lifetime regressions. Latest user confirmation concerns PVP performance.'
            files = $files
        }
        [IO.File]::WriteAllText((Join-Path $package 'release.json'), ($manifest | ConvertTo-Json -Depth 6), $utf8)
    }
    Write-Manifest

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
    Copy-Item -LiteralPath (Join-Path $testRun 'gui-validation.json'), (Join-Path $testRun 'gui-preview.png') -Destination (Join-Path $package 'verification')

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
    $fixtureResults = Join-Path $package 'source\build\release-fixtures'
    foreach ($name in @('summary.json','mapped-sampler.json','mapped-memory.json')) {
        Copy-Item -LiteralPath (Join-Path $fixtureResults $name) -Destination (Join-Path $package ('verification\' + $name))
    }
    Copy-Item -LiteralPath (Join-Path $fixtureResults 'regular-sampler\driver-test.json') -Destination (Join-Path $package 'verification\regular-sampler-driver.json')
    # Build products belong to verification, not the source distribution.
    $generatedBuild = [IO.Path]::GetFullPath((Join-Path $package 'source\build'))
    if ($generatedBuild -ne ($package + '\source\build')) { throw 'Unexpected fixture output directory.' }
    Remove-Item -LiteralPath $generatedBuild -Recurse -Force
    [ordered]@{
        renderer_commit = $revision; renderer_sha256 = (Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash
        source_sha256 = (Get-FileHash -LiteralPath (Join-Path $package 'source\shim.c') -Algorithm SHA256).Hash
        release_matches_deployed_renderer_except_pe_metadata = $true
        packaged_source_rebuild_matches_except_pe_metadata = $true
        gui_integration_tests_passed = $true; external_marker_files_required = $false
        packaged_driver_fixtures_passed = $true; packaged_dll_byte_identical_to_tested = $true
        capture_layer_packaged = $false; live_game_modified = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'verification\release-audit.json') -Encoding UTF8
    Write-Manifest
    $hashLines = @(
        foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName) {
            '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash, $file.FullName.Substring($package.Length + 1).Replace('\', '/')
        }
    )
    [IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'), [string[]]$hashLines, $utf8)
    Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
    $zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    [IO.File]::WriteAllText(($zip + '.sha256'), ($zipHash + '  ' + (Split-Path -Leaf $zip) + "`r`n"), $utf8)
    [pscustomobject]@{ Package = $package; Zip = $zip; SourceCommit = $revision; ZipSHA256 = $zipHash }
} finally { Pop-Location }
