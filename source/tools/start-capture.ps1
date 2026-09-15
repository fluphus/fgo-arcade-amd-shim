[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [ValidateRange(1,86400)][int]$Seconds = 600,
    [string]$OutputDirectory,
    [switch]$MappedLifetimeAudit
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$root = Split-Path -Parent $PSScriptRoot
$app = (Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $app 'ago.exe'))) { $app = Join-Path $app 'App' }
$game = @(Get-Process -Name ago -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $app 'ago.exe') })
if ($game.Count -ne 1) { throw 'Start the selected game first. Exactly one matching ago.exe is required.' }
$existing = @(Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" | Where-Object {
    $_.CommandLine -match 'battle_counter_live.py|battle_lowfps_stacks.py'
})
if ($existing.Count) { throw 'A collector is already running. Stop its capture before starting another.' }
$python = (Get-Command python.exe -ErrorAction Stop).Source
$null = Get-Command llvm-nm.exe -ErrorAction Stop
& $python -c "import struct, pefile; assert struct.calcsize('P') == 8"
if ($LASTEXITCODE -ne 0) { throw 'Install 64-bit Python and pefile (python -m pip install pefile).' }
$sampler = Join-Path $PSScriptRoot 'bin\battle_cpu_sample.exe'
if (-not (Test-Path -LiteralPath $sampler)) { throw 'Build the sampler using tools\build-tools.ps1 first.' }
$capture = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $root ('captures\battle_' + (Get-Date -Format yyyyMMdd_HHmmss))
}
if (Test-Path -LiteralPath $capture) { throw 'Use a new output directory for each capture.' }
$renderer = Join-Path $app 'opengl32.dll'
$modules = @($game[0].Modules | ForEach-Object {
    [ordered]@{ ModuleName=$_.ModuleName; FileName=$_.FileName; Base=$_.BaseAddress.ToInt64(); Size=$_.ModuleMemorySize }
})
$loaded = @($modules | Where-Object { $_.FileName -eq $renderer })
if ($loaded.Count -ne 1) { throw 'The selected game has not loaded the patch DLL.' }
$programSymbol = @(& llvm-nm.exe --defined-only $renderer | Select-String ' g_current_program$')
if ($LASTEXITCODE -ne 0 -or $programSymbol.Count -ne 1) { throw 'Renderer symbols are missing or incompatible.' }
if ($MappedLifetimeAudit) {
    $auditSymbols = @(& llvm-nm.exe --defined-only $renderer | Select-String ' g_perf_mapped_audit_(reads|flushes|changed|mismatches)$')
    if ($LASTEXITCODE -ne 0 -or $auditSymbols.Count -ne 4) { throw 'This DLL does not contain the mapped lifetime diagnostic. No capture started.' }
}
$imageBase = & $python -c 'import pefile, sys; print(pefile.PE(sys.argv[1], fast_load=True).OPTIONAL_HEADER.ImageBase)' $renderer
if ($LASTEXITCODE -ne 0) { throw 'Cannot read renderer image base.' }
$programAddress = '0x{0:x}' -f ($loaded[0].Base + [Convert]::ToInt64(($programSymbol[0].Line -split '\s+')[0],16) - [long]$imageBase)
New-Item -ItemType Directory -Path $capture | Out-Null
$pinned = Join-Path $capture 'captured-opengl32.dll'
Copy-Item -LiteralPath $renderer -Destination $pinned
$modules | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $capture 'modules.json') -Encoding UTF8
$manifest = [ordered]@{
    started=[DateTimeOffset]::Now.ToString('o'); expected_end=[DateTimeOffset]::Now.AddSeconds($Seconds).ToString('o')
    game_pid=$game[0].Id; renderer=$renderer; renderer_sha256=(Get-FileHash -LiteralPath $pinned -Algorithm SHA256).Hash
    seconds=$Seconds; poll_ms=250; program_address=$programAddress
    mapped_lifetime_audit=$MappedLifetimeAudit.IsPresent
}
$manifestPath = Join-Path $capture 'manifest.json'
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
function Quote-Argument([string]$Value) {
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Start-Observer([string[]]$Arguments, [string]$LogName) {
    # Python opens log paths literally; PowerShell redirection treats [] as wildcards.
    $bootstrap = "import sys,runpy; sys.stdout=open(sys.argv[1],'w',encoding='utf-8',buffering=1); sys.stderr=open(sys.argv[2],'w',encoding='utf-8',buffering=1); sys.argv=sys.argv[3:]; runpy.run_path(sys.argv[0],run_name='__main__')"
    $argv = @('-u','-c',$bootstrap,(Join-Path $capture ($LogName + '.stdout.log')),
        (Join-Path $capture ($LogName + '.stderr.log'))) + $Arguments
    $line = ($argv | ForEach-Object { Quote-Argument $_ }) -join ' '
    Start-Process -FilePath $python -ArgumentList $line -WorkingDirectory $root -WindowStyle Hidden -PassThru
}
try {
    $collector = Start-Observer @((Join-Path $root 'tests\battle_counter_live.py'),
        '--pid',"$($game[0].Id)",'--modules',(Join-Path $capture 'modules.json'),'--renderer',$renderer,
        '--symbol-file',$pinned,'--seconds',"$Seconds",'--output',(Join-Path $capture 'interval_samples.jsonl')) 'counter'
    $watcher = Start-Observer @((Join-Path $root 'tests\battle_lowfps_stacks.py'),
        '--capture',$capture,'--sampler',$sampler,'--program-address',$programAddress) 'watcher'
    $manifest.collector_pid = $collector.Id
    $manifest.stack_watcher_pid = $watcher.Id
    $complete = Join-Path $capture 'manifest.complete.json'
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $complete -Encoding UTF8
    [IO.File]::Replace($complete, $manifestPath, (Join-Path $capture 'manifest.initial.json'))
    Start-Sleep -Milliseconds 800
    if ($collector.HasExited -or $watcher.HasExited) { throw 'A collector exited early. See counter/watcher stderr logs.' }
} catch {
    [IO.File]::WriteAllText((Join-Path $capture 'stop_capture'), '')
    throw
}
Write-Output "CAPTURE=$capture"
Write-Output "START=$($manifest.started) END=$($manifest.expected_end)"
Write-Output 'Rows are written continuously. Use tools\stop-capture.ps1 -CaptureDirectory <CAPTURE> to stop early.'
