[CmdletBinding()]
param([switch]$Driver, [switch]$Benchmarks)
$ErrorActionPreference = 'Stop'
$previousBytecode = $env:PYTHONDONTWRITEBYTECODE
$env:PYTHONDONTWRITEBYTECODE = '1'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\release-fixtures'
$fixtures = Join-Path $root 'tests\fixtures'
$cc = (Get-Command x86_64-w64-mingw32-gcc.exe -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $build -Force | Out-Null
function Compile([string]$Source, [string]$Name, [string[]]$Flags = @()) {
    $output = Join-Path $build $Name
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $cc -O2 @Flags (Join-Path $root ('tests\' + $Source)) `
            (Join-Path $root 'present_dxgi_latency1.c') -o $output `
            -lgdi32 -luser32 -ld3d11 -ldxgi -ldxguid -lole32 *> ($output + '.build.log')
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($code -ne 0) { throw "Compile failed: $Source. See $output.build.log" }
    return $output
}
Push-Location $root
try {
    $results = @()
    foreach ($name in @('pointer_content_cache_test', 'emitter_header_cache_test', 'battle_emitter_pointer_test', 'frame_pacing_test')) {
        $exe = Compile ($name + '.c') ($name + '.exe')
        $output = Join-Path $build $name
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        if ($name -in @('pointer_content_cache_test','frame_pacing_test')) { & $exe *> (Join-Path $output 'result.log') }
        else { & $exe $output (Join-Path $fixtures 'particle') *> (Join-Path $output 'result.log') }
        if ($LASTEXITCODE -ne 0) { throw "Fixture failed: $name. See $output\result.log" }
        $results += $name
        Write-Output "PASS $name"
    }
    if ($Driver -or $Benchmarks) {
        & python -c 'import pefile'
        if ($LASTEXITCODE -ne 0) { throw 'Install 64-bit Python and pefile.' }
    }
    if ($Driver) {
        $wrapper = Compile 'mapped_sampler_read_bench.c' 'mapped_sampler.dll' @('-shared')
        & python tests\mapped_sampler_read_bench.py --wrapper $wrapper --output (Join-Path $build 'mapped-sampler.json') *> (Join-Path $build 'mapped-sampler.log')
        if ($LASTEXITCODE -ne 0) { throw 'Mapped sampler driver regression failed; see mapped-sampler.log.' }
        $runner = Compile 'driver_residency_benchmark.c' 'residency_benchmark.dll' @('-shared')
        $scopeWrapper = Compile 'sampler_tile_wrapper.c' 'sampler_tile.dll' @('-shared')
        & python tests\regular_sampler_driver_test.py --scope-regression --wrapper $scopeWrapper --runner $runner --fixture-root (Join-Path $fixtures 'model') --output (Join-Path $build 'regular-sampler') *> (Join-Path $build 'regular-sampler.log')
        if ($LASTEXITCODE -ne 0) { throw 'Regular sampler driver regression failed; see regular-sampler.log.' }
        $results += 'mapped_sampler_driver', 'regular_sampler_driver'
        Write-Output 'PASS mapped_sampler_driver, regular_sampler_driver'
        foreach ($variant in @('', '--no-near')) {
            $tileArgs = @('tests\tile_depth_driver_test.py','--wrapper',$scopeWrapper,'--output',(Join-Path $build 'tile-depth'))
            if ($variant) { $tileArgs += $variant }
            & python @tileArgs *> (Join-Path $build ('tile-depth' + $variant + '.log'))
            if ($LASTEXITCODE -ne 0) { throw 'Tile-depth driver regression failed.' }
        }
        $split = Compile 'split_blit_wrapper.c' 'split_blit.dll' @('-shared')
        & python tests\split_blit_driver_test.py $split --output (Join-Path $build 'split-blit.json') *> (Join-Path $build 'split-blit.log')
        if ($LASTEXITCODE -ne 0) { throw 'Split-blit driver regression failed.' }
        $results += 'tile_depth_driver','split_blit_driver'
        Write-Output 'PASS tile_depth_driver, split_blit_driver'
    }
    if ($Benchmarks) {
        $wrapper = Compile 'mapped_memory_read_bench.c' 'mapped_memory.dll' @('-shared', '-msse4.1')
        & python tests\mapped_memory_read_bench.py --dll $wrapper --stream-only --output (Join-Path $build 'mapped-memory.json') *> (Join-Path $build 'mapped-memory.log')
        if ($LASTEXITCODE -ne 0) { throw 'Mapped memory benchmark failed; see mapped-memory.log.' }
        $results += 'mapped_memory_benchmark'
        Write-Output 'PASS mapped_memory_benchmark'
    }
    [ordered]@{ passed=$true; checks=$results; driver_tests=[bool]$Driver; benchmarks=[bool]$Benchmarks } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'summary.json') -Encoding UTF8
} finally { Pop-Location; $env:PYTHONDONTWRITEBYTECODE = $previousBytecode }
