# Shared helpers for resolving where a mod's canonical source lives.
#
# Manifests reference mods by name and may override the source root per mod
# via "sourceRoot". The default root is settings.northstarModsRoot (the PC
# Northstar install). A sourceRoot that is not an absolute path is resolved
# relative to the repository root, which lets PS4-port-specific mods live in
# the repo (mods/<ModName>) while PC mods keep staging straight from the PC
# install. This keeps the PS4 /app0/mods layout an exact mirror of the PC
# mod layout.
#
# Callers dot-source this file and invoke the functions below.

function Resolve-ModSourceRoot {
    param(
        [Parameter(Mandatory = $true)] $Settings,
        [Parameter(Mandatory = $true)] $Mod,
        [Parameter(Mandatory = $true)] $RepositoryRoot
    )
    $sourceRoot = $null
    if ($Mod.PSObject.Properties.Name -contains 'sourceRoot' -and -not [string]::IsNullOrWhiteSpace($Mod.sourceRoot)) {
        $sourceRoot = $Mod.sourceRoot
    }
    if ([string]::IsNullOrWhiteSpace($sourceRoot)) {
        return $Settings.northstarModsRoot
    }
    if ([System.IO.Path]::IsPathRooted($sourceRoot)) {
        return [System.IO.Path]::GetFullPath($sourceRoot)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepositoryRoot $sourceRoot))
}

function Get-ModSourceDir {
    param(
        [Parameter(Mandatory = $true)] $Settings,
        [Parameter(Mandatory = $true)] $Mod,
        [Parameter(Mandatory = $true)] $RepositoryRoot
    )
    $root = Resolve-ModSourceRoot -Settings $Settings -Mod $Mod -RepositoryRoot $RepositoryRoot
    $subdirectory = if ($Mod.PSObject.Properties.Name -contains 'sourceSubdirectory' -and -not [string]::IsNullOrWhiteSpace($Mod.sourceSubdirectory)) { $Mod.sourceSubdirectory } else { $Mod.name }
    return [System.IO.Path]::GetFullPath((Join-Path $root $subdirectory))
}

function Get-ModJsonPath {
    param(
        [Parameter(Mandatory = $true)] $Settings,
        [Parameter(Mandatory = $true)] $Mod,
        [Parameter(Mandatory = $true)] $RepositoryRoot
    )
    return [System.IO.Path]::GetFullPath((Join-Path (Get-ModSourceDir -Settings $Settings -Mod $Mod -RepositoryRoot $RepositoryRoot) 'mod.json'))
}
