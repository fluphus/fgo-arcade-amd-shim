[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Install', 'Restore')][string]$Action,
    [Parameter(Mandatory)][string]$GameApp
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$response = [ordered]@{ ok = $false; message = ''; gameApp = $GameApp; backup = $null; sha256 = $null }
$exitCode = 1
try {
    $app = (Resolve-Path -LiteralPath $GameApp).ProviderPath
    if (-not (Test-Path -LiteralPath (Join-Path $app 'ago.exe') -PathType Leaf)) {
        throw 'The selected directory does not contain ago.exe.'
    }
    $installer = Join-Path $PSScriptRoot 'install.ps1'
    if ($Action -eq 'Install') {
        $result = & $installer -GameApp $app -Confirm:$false
        $response.message = 'Installation completed. Original patch files have been backed up.'
        $response.sha256 = $result.SHA256
    } else {
        $backupRoot = Join-Path $app 'shim-backups'
        $latest = $null
        if (Test-Path -LiteralPath $backupRoot -PathType Container) {
            foreach ($directory in @(Get-ChildItem -LiteralPath $backupRoot -Directory | Sort-Object CreationTimeUtc, Name -Descending)) {
                $manifestPath = Join-Path $directory.FullName 'backup.json'
                if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
                $snapshot = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($snapshot.schema -in @(2,3) -and [IO.Path]::GetFullPath($snapshot.app).TrimEnd('\') -eq $app.TrimEnd('\')) {
                    $latest = $directory.FullName
                    break
                }
            }
        }
        if (-not $latest) { throw 'No portable-release backup was found for this game directory.' }
        $result = & $installer -RestoreBackup $latest -Confirm:$false
        $response.message = 'The patch files saved before the last installation have been restored.'
    }
    $response.ok = $true
    $response.gameApp = $result.GameApp
    $response.backup = $result.Backup
    $exitCode = 0
} catch {
    $response.message = $_.Exception.Message
}
[Console]::Out.WriteLine(($response | ConvertTo-Json -Depth 4 -Compress))
exit $exitCode
