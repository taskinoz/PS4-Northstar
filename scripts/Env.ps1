# Settings for the development scripts, dot-sourced by them (`. "$PSScriptRoot\Env.ps1"`).
#
# Settings come from environment variables. A `.env` file at the repository
# root (KEY=value lines; see `.env.example`) is read first, and a variable
# already set in the environment takes precedence over it. `.env` is ignored by
# git, so paths on one machine never reach the repository.

$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

$envFile = Join-Path $RepoRoot '.env'
if (Test-Path -LiteralPath $envFile -PathType Leaf) {
    foreach ($line in Get-Content -LiteralPath $envFile) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('#')) { continue }
        $equals = $text.IndexOf('=')
        if ($equals -lt 1) { continue }
        $name = $text.Substring(0, $equals).Trim()
        $value = $text.Substring($equals + 1).Trim().Trim('"').Trim("'")
        if (-not [Environment]::GetEnvironmentVariable($name)) { [Environment]::SetEnvironmentVariable($name, $value) }
    }
}

# A setting's value, or $Default when it is unset.
function Get-NorthstarSetting([string] $Name, [string] $Default = '') {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($value) { return $value }
    return $Default
}

# A setting that a script cannot run without.
function Get-RequiredNorthstarSetting([string] $Name, [string] $Purpose) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if (-not $value) { throw "$Name is not set: $Purpose. Set it in the environment or in .env (see .env.example)." }
    return $value
}

# The shadPS4 game folder: the folder holding the game's eboot.bin and vpk_ps4.
function Get-GameRoot {
    [IO.Path]::GetFullPath((Get-RequiredNorthstarSetting 'NORTHSTAR_PS4_GAME_ROOT' 'the shadPS4 game folder (the one with eboot.bin)'))
}

# Northstar's mods: Build-NorthstarMods.py writes them to work/northstar-release/<version>/mods.
function Get-NorthstarModsRoot {
    [IO.Path]::GetFullPath((Get-NorthstarSetting 'NORTHSTAR_MODS_ROOT' (Join-Path $RepoRoot 'work\northstar-release\1.31.13\mods')))
}

function Get-ShadPs4Exe {
    [IO.Path]::GetFullPath((Get-RequiredNorthstarSetting 'SHADPS4_EXE' 'the shadPS4.exe to launch'))
}

# shadPS4's user folder, holding log\shad_log.txt and data\northstar_ps4.
function Get-ShadPs4UserDir {
    [IO.Path]::GetFullPath((Get-NorthstarSetting 'SHADPS4_USER_DIR' (Join-Path $env:APPDATA 'shadPS4')))
}
