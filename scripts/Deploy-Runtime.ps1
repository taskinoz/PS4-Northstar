# Copies a built runtime into the shadPS4 game folder (bin\ps4_retail), keeping
# the previous one under work\deploy-backups, and checks the installed hash.
# The game loads it through the eboot bootstrap (Enable-Bootstrap.ps1).
[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Source = (Join-Path $PSScriptRoot '..\dist\northstar-ps4\northstar_ps4.prx'),
    [string] $GameRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Env.ps1"
if (-not $GameRoot) { $GameRoot = Get-GameRoot }
$sourcePath = [IO.Path]::GetFullPath($Source)
$destination = Join-Path ([IO.Path]::GetFullPath($GameRoot)) 'bin\ps4_retail\northstar_ps4.prx'
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Runtime not found: $sourcePath" }
if (Test-Path -LiteralPath $destination -PathType Leaf) {
    $backupRoot = Join-Path $RepoRoot ('work\deploy-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    [IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backupRoot -Force
}
if ($PSCmdlet.ShouldProcess($destination, "Install the runtime from $sourcePath")) {
    [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
}
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
if ($sourceHash -ne $installedHash) { throw "Installed runtime hash mismatch: $destination" }
[pscustomobject]@{ Installed = $destination; SHA256 = $installedHash.ToLowerInvariant() }
