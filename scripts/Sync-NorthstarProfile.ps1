<#
.SYNOPSIS
Brings a deployed PS4 mod profile back in line with its sources.

.DESCRIPTION
New-NorthstarProfile.ps1 only writes new profiles. This script refreshes a
deployed one: it compares every source file with the deployment, copies what is
missing or changed, checks each copy by hash, and writes a manifest, so the next
run reports drift.

Mods come from the same two places as New-NorthstarProfile.ps1: Northstar's mods
(NORTHSTAR_MODS_ROOT, or -ModsRoot), and this repository's mods/ for the
port's own (Northstar.PS4, Northstar.DirectConnect). A folder in the repository
wins over one of the same name from Northstar's mods.

Files the game writes itself are never touched. Only mods/ is synchronised;
enabledmods.json is left alone once it exists, because it holds the player's own
choices.

.PARAMETER Prune
Delete files in the deployment that no longer exist in any source. Off by
default: a stale file is usually harmless, and deleting from a game directory
deserves an explicit ask.

.EXAMPLE
scripts\Sync-NorthstarProfile.ps1 -WhatIf
Report drift without changing anything.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $ModsRoot,
    # Defaults to R2Northstar in the shadPS4 game folder.
    [string] $Destination,
    [switch] $IncludePs4CompatibilityMods = $true,
    [switch] $ConvertRpaksForPs4,
    [switch] $Prune
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Env.ps1"
$repositoryRoot = $RepoRoot
if (-not $ModsRoot) { $ModsRoot = Get-NorthstarModsRoot }
if (-not $Destination) { $Destination = Join-Path (Get-GameRoot) 'R2Northstar' }
$Destination = [IO.Path]::GetFullPath($Destination)
$modsDestination = Join-Path $Destination 'mods'

# Lowest priority first, so a later source overrides an earlier one by folder
# name. The repository's own mods come last and therefore win.
$sources = @([IO.Path]::GetFullPath($ModsRoot))
if ($IncludePs4CompatibilityMods) { $sources += (Join-Path $repositoryRoot 'mods') }
foreach ($source in $sources) {
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Mod source not found: $source" }
}

# Build the intended file set: relative path -> source file.
$intended = [ordered]@{}
$modOrigin = [ordered]@{}
$conversionRoot = if ($ConvertRpaksForPs4) {
    Join-Path $repositoryRoot ('work\sync-rpak-conversion\' + [guid]::NewGuid().ToString('N'))
} else { $null }
foreach ($source in $sources) {
    foreach ($directory in (Get-ChildItem -LiteralPath $source -Directory | Sort-Object Name)) {
        if (-not (Test-Path -LiteralPath (Join-Path $directory.FullName 'mod.json') -PathType Leaf)) { continue }
        $modOrigin[$directory.Name] = $source
        foreach ($file in (Get-ChildItem -LiteralPath $directory.FullName -Recurse -File -Force)) {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Mod contains a link; supply regular files: $($file.FullName)"
            }
            $relative = $directory.Name + '\' + $file.FullName.Substring($directory.FullName.Length + 1)
            $intended[$relative] = $file.FullName
        }
        if ($ConvertRpaksForPs4 -and (Test-Path -LiteralPath (Join-Path $directory.FullName 'paks') -PathType Container)) {
            $overlay = Join-Path $conversionRoot ($directory.Name + '-' + [guid]::NewGuid().ToString('N'))
            & (Join-Path $PSScriptRoot 'Convert-NorthstarModRpaks.ps1') -SourceModDirectory $directory.FullName -Output $overlay | Out-Host
            foreach ($file in Get-ChildItem -LiteralPath $overlay -File) {
                $relative = $directory.Name + '\paks\' + $file.Name
                $intended[$relative] = $file.FullName
            }
        }
    }
}
if ($intended.Count -eq 0) { throw "No mod.json folders found in: $($sources -join ', ')" }

function Get-Sha256([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

$added = [Collections.Generic.List[string]]::new()
$updated = [Collections.Generic.List[string]]::new()
$removed = [Collections.Generic.List[string]]::new()
$unchanged = 0
$manifest = [Collections.Generic.List[object]]::new()

foreach ($relative in $intended.Keys) {
    $sourceFile = $intended[$relative]
    $destinationFile = Join-Path $modsDestination $relative
    $expected = Get-Sha256 $sourceFile
    $manifest.Add([pscustomobject]@{ Path = "mods/$($relative.Replace('\','/'))"; SHA256 = $expected })

    $state = 'unchanged'
    if (-not (Test-Path -LiteralPath $destinationFile -PathType Leaf)) { $state = 'added' }
    elseif ((Get-Sha256 $destinationFile) -ne $expected) { $state = 'updated' }
    if ($state -eq 'unchanged') { $unchanged++; continue }

    if ($PSCmdlet.ShouldProcess($destinationFile, "Copy ($state)")) {
        New-Item -ItemType Directory -Path (Split-Path $destinationFile -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force
        # Verify rather than trust: a short copy into a game directory is worse
        # than a failed one, because it looks like it worked.
        if ((Get-Sha256 $destinationFile) -ne $expected) { throw "Copy verification failed: $destinationFile" }
    }
    if ($state -eq 'added') { $added.Add($relative) } else { $updated.Add($relative) }
}

if (Test-Path -LiteralPath $modsDestination -PathType Container) {
    foreach ($file in (Get-ChildItem -LiteralPath $modsDestination -Recurse -File -Force)) {
        $relative = $file.FullName.Substring($modsDestination.Length + 1)
        if ($relative -eq '.ns_mod_manifest' -or $relative -eq 'profile-files.json') { continue }
        if ($intended.Contains($relative)) { continue }
        $removed.Add($relative)
        if ($Prune -and $PSCmdlet.ShouldProcess($file.FullName, 'Remove (not in any source)')) {
            Remove-Item -LiteralPath $file.FullName -Force
        }
    }
}

if ($PSCmdlet.ShouldProcess($modsDestination, 'Write manifests')) {
    New-Item -ItemType Directory -Path $modsDestination -Force | Out-Null
    $utf8 = [Text.UTF8Encoding]::new($false)
    # Only used when the emulator cannot enumerate directories; not an allowlist.
    [IO.File]::WriteAllLines((Join-Path $modsDestination '.ns_mod_manifest'),
        [string[]]@($modOrigin.Keys), $utf8)
    # Deployed alongside the mods, unlike New-NorthstarProfile.ps1's copy, so
    # the next run can tell drift from a fresh install.
    [IO.File]::WriteAllText((Join-Path $modsDestination 'profile-files.json'),
        ($manifest | ConvertTo-Json -Depth 4), $utf8)
}

foreach ($entry in $added) { Write-Verbose "added   $entry" }
foreach ($entry in $updated) { Write-Verbose "updated $entry" }
foreach ($entry in $removed) { Write-Verbose "stale   $entry" }
if ($conversionRoot -and (Test-Path -LiteralPath $conversionRoot)) {
    Remove-Item -LiteralPath $conversionRoot -Recurse -Force
}

[pscustomobject]@{
    Destination = $modsDestination
    Mods        = $modOrigin.Count
    Added       = $added.Count
    Updated     = $updated.Count
    Unchanged   = $unchanged
    Stale       = $removed.Count
    Pruned      = if ($Prune) { $removed.Count } else { 0 }
    Ps4Rpaks    = [bool]$ConvertRpaksForPs4
    InSync      = ($added.Count -eq 0 -and $updated.Count -eq 0 -and ($removed.Count -eq 0 -or -not $Prune))
}
