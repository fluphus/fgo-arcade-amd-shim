[CmdletBinding(SupportsShouldProcess, DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Install')]
    [string]$GameApp,
    [Parameter(Mandatory, ParameterSetName = 'Stage')]
    [string]$StagingRoot,
    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [string]$RestoreBackup
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$dllNames = @('opengl32.dll', 'opengl32real.dll')
$patchNames = $dllNames + @('amdcfg/amdOglpSettings.cfg')

function Get-Digest([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-GameStopped([string]$App) {
    foreach ($process in @(Get-Process -Name ago -ErrorAction SilentlyContinue)) {
        if (-not $process.Path -or (Split-Path -Parent $process.Path) -eq $App) {
            throw 'Close the game (ago.exe) before replacing its DLLs.'
        }
    }
}

function Restore-Snapshot($Snapshot, [string]$BackupDirectory) {
    foreach ($entry in $Snapshot.dlls) {
        if ($entry.exists) {
            $saved = Join-Path $BackupDirectory $entry.backup
            if ((Get-Digest $saved) -ne $entry.sha256) { throw "Backup hash mismatch: $saved" }
        }
    }
    foreach ($entry in $Snapshot.dlls) {
        $target = Join-Path $Snapshot.app $entry.name
        if ($entry.exists) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $BackupDirectory $entry.backup) -Destination $target -Force
        } elseif (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target
        }
    }
    if ($Snapshot.schema -eq 3 -and -not $Snapshot.amdcfg_existed) {
        $config = Join-Path $Snapshot.app 'amdcfg'
        if ((Test-Path -LiteralPath $config) -and
            @(Get-ChildItem -LiteralPath $config -Force).Count -eq 0) {
            Remove-Item -LiteralPath $config
        }
    }
}

if ($PSCmdlet.ParameterSetName -eq 'Restore') {
    $backup = (Resolve-Path -LiteralPath $RestoreBackup).Path
    $snapshot = Get-Content -LiteralPath (Join-Path $backup 'backup.json') -Raw | ConvertFrom-Json
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $snapshot.app 'shim-backups'))
    if ((Split-Path -Parent $backup) -ne $expectedParent -or $snapshot.schema -notin @(2,3)) {
        throw 'Use a portable-release backup under its original App\shim-backups directory. Older backups require their original installer.'
    }
    $expectedNames = if ($snapshot.schema -eq 3) { $patchNames } else { $dllNames }
    if (@($snapshot.dlls).Count -ne $expectedNames.Count -or
        @($snapshot.dlls.name | Sort-Object -Unique).Count -ne $expectedNames.Count) {
        throw 'Invalid DLL backup inventory.'
    }
    foreach ($entry in $snapshot.dlls) {
        if ($entry.name -notin $expectedNames -or $entry.backup -ne ('dlls/' + $entry.name)) { throw 'Invalid patch backup entry.' }
    }
    if (-not $PSCmdlet.ShouldProcess($snapshot.app, "Restore shim DLLs from $backup")) { return }
    if (-not $snapshot.staging) { Assert-GameStopped $snapshot.app }
    Restore-Snapshot $snapshot $backup
    [pscustomobject]@{ Result = 'Restored'; GameApp = $snapshot.app; Backup = $backup }
    return
}

$packageRoot = Split-Path -Parent $PSScriptRoot
$checksumsPath = Join-Path $packageRoot 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $checksumsPath -PathType Leaf)) {
    throw 'Package checksum list is missing.'
}
$checksums = @{}
foreach ($line in Get-Content -LiteralPath $checksumsPath) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split '\s+', 2
    if ($parts.Count -eq 2 -and $parts[0] -match '^[0-9A-Fa-f]{64}$') {
        $checksums[$parts[1].TrimStart('*')] = $parts[0].ToUpperInvariant()
    }
}
foreach ($relative in @('game-patch/opengl32.dll', 'game-patch/install.ps1', 'game-patch/amdcfg/amdOglpSettings.cfg')) {
    if (-not $checksums.ContainsKey($relative)) {
        throw "Package checksum is missing: $relative"
    }
    if ((Get-Digest (Join-Path $packageRoot $relative)) -ne $checksums[$relative]) {
        throw "Package hash mismatch: $relative"
    }
}

$staging = $PSCmdlet.ParameterSetName -eq 'Stage'
if ($staging) {
    $stageRoot = [IO.Path]::GetFullPath($StagingRoot)
    $app = Join-Path $stageRoot 'App'
} else {
    $app = (Resolve-Path -LiteralPath $GameApp).Path.TrimEnd('\')
    if (-not (Test-Path -LiteralPath (Join-Path $app 'ago.exe') -PathType Leaf)) {
        throw 'GameApp must be the App directory containing ago.exe.'
    }
}
$systemOpenGL = Join-Path ([Environment]::SystemDirectory) 'opengl32.dll'
$systemHash = Get-Digest $systemOpenGL
$candidate = Join-Path $PSScriptRoot 'opengl32.dll'
$candidateHash = Get-Digest $candidate
if (-not $PSCmdlet.ShouldProcess($app, 'Install embedded 60 Hz OpenGL DLLs')) { return }
if (-not $staging) { Assert-GameStopped $app }

$backupName = (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$backup = Join-Path $app ('shim-backups\' + $backupName)
New-Item -ItemType Directory -Path (Join-Path $backup 'dlls') -Force | Out-Null
$configExisted = Test-Path -LiteralPath (Join-Path $app 'amdcfg') -PathType Container
$savedDlls = @(
    foreach ($name in $patchNames) {
        $target = Join-Path $app $name
        $exists = Test-Path -LiteralPath $target -PathType Leaf
        $hash = $null
        if ($exists) {
            $hash = Get-Digest $target
            $saved = Join-Path $backup ('dlls/' + $name)
            New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
        }
        [ordered]@{ name = $name; exists = $exists; sha256 = $hash; backup = 'dlls/' + $name }
    }
)
$snapshot = [pscustomobject]@{
    schema = 3; baseline = 'embedded-60hz'; app = $app; amdcfg_existed = $configExisted
    staging = $staging; dlls = $savedDlls
    installed_sha256 = $candidateHash; system_opengl_sha256 = $systemHash
}
$snapshot | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'backup.json') -Encoding UTF8
foreach ($entry in $savedDlls) {
    if ($entry.exists -and (Get-Digest (Join-Path $backup $entry.backup)) -ne $entry.sha256) {
        throw 'Backup verification failed before installation.'
    }
}

try {
    Copy-Item -LiteralPath $systemOpenGL -Destination (Join-Path $app 'opengl32real.dll') -Force
    Copy-Item -LiteralPath $candidate -Destination (Join-Path $app 'opengl32.dll') -Force
    New-Item -ItemType Directory -Path (Join-Path $app 'amdcfg') -Force | Out-Null
    $configSource = Join-Path $PSScriptRoot 'amdcfg/amdOglpSettings.cfg'
    $configTarget = Join-Path $app 'amdcfg/amdOglpSettings.cfg'
    Copy-Item -LiteralPath $configSource -Destination $configTarget -Force
    if ((Get-Digest (Join-Path $app 'opengl32.dll')) -ne $candidateHash -or
        (Get-Digest (Join-Path $app 'opengl32real.dll')) -ne $systemHash -or
        (Get-Digest $configTarget) -ne (Get-Digest $configSource)) { throw 'Installed patch verification failed.' }
} catch {
    $failure = $_
    try { Restore-Snapshot $snapshot $backup } catch { throw "Install failed: $failure. Restore also failed: $_. Backup: $backup" }
    throw "Installation failed and the old files were restored: $failure"
}
[pscustomobject]@{ Result = 'Installed'; GameApp = $app; Backup = $backup; SHA256 = $candidateHash; Staging = $staging }
