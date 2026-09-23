[CmdletBinding()]
param([Parameter(Mandatory)][string]$CaptureDirectory)
$ErrorActionPreference = 'Stop'
$capture = (Resolve-Path -LiteralPath $CaptureDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $capture 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
[IO.File]::WriteAllText((Join-Path $capture 'stop_capture'), '')
if ($manifest.presentmon_pid -and (Get-Process -Id $manifest.presentmon_pid -ErrorAction SilentlyContinue)) {
    & $manifest.presentmon_executable --session_name $manifest.presentmon_session --terminate_existing_session
    if ($LASTEXITCODE -ne 0) { throw 'PresentMon stop failed; leave the session running and inspect its log.' }
}
$observerIds = @($manifest.collector_pid, $manifest.stack_watcher_pid, $manifest.presentmon_pid) | Where-Object { $_ }
$deadline = [DateTime]::UtcNow.AddSeconds(20)
do {
    $running = @($observerIds | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
    if (-not $running.Count) { break }
    Start-Sleep -Milliseconds 200
} while ([DateTime]::UtcNow -lt $deadline)
if ($running.Count) { throw 'Stop requested; a sampler is still closing. Rows are already on disk. Do not force-kill a stack sampler.' }
Write-Output "Capture stopped; data retained in $capture"
