[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Source = (Join-Path $PSScriptRoot '..\dist\stage2-poc\northstar_ps4.prx')
)
$ErrorActionPreference = 'Stop'
$settings = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
$sourcePath = [IO.Path]::GetFullPath($Source)
$destination = Join-Path ([IO.Path]::GetFullPath($settings.ps4GameRoot)) 'bin\ps4_retail\northstar_ps4.prx'
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Stage 2 PoC PRX not found: $sourcePath" }
if (Test-Path -LiteralPath $destination -PathType Leaf) {
    $backupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\work\stage2\deploy-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))))
    [IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backupRoot -Force
}
if ($PSCmdlet.ShouldProcess($destination, "Stage inert PoC module from $sourcePath")) { Copy-Item -LiteralPath $sourcePath -Destination $destination -Force }
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
if ($sourceHash -ne $installedHash) { throw "Stage 2 PoC deployment hash mismatch: $destination" }
[pscustomobject]@{ Installed=$destination; SHA256=$installedHash.ToLowerInvariant(); Active=$true; Note='PRX is loaded by the hash-locked eboot Stage 2 bootstrap.' }
