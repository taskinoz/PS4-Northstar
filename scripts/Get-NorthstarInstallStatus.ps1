[CmdletBinding()]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $GameRoot,
    [string] $BuildInfo
)
$ErrorActionPreference = 'Stop'
if (-not $GameRoot) {
    $settings = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
    $GameRoot = $settings.ps4GameRoot
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)
$prx = Join-Path $GameRoot 'bin\ps4_retail\northstar_ps4.prx'
$hash = if (Test-Path -LiteralPath $prx -PathType Leaf) { (Get-FileHash -LiteralPath $prx -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
$matched = $null
$infoPath = $null
if ($BuildInfo) {
    $info = Get-Content -LiteralPath $BuildInfo -Raw | ConvertFrom-Json
    if ($hash -and $info.prxSha256 -eq $hash) { $matched = $info; $infoPath = [IO.Path]::GetFullPath($BuildInfo) }
    else { throw 'Build information does not match the installed PRX hash.' }
} elseif ($hash) {
    $dist = Join-Path $PSScriptRoot '..\dist'
    if (Test-Path -LiteralPath $dist) {
        foreach ($candidate in Get-ChildItem -LiteralPath $dist -Filter 'northstar_ps4.build.json' -Recurse -File) {
            try { $info = Get-Content -LiteralPath $candidate.FullName -Raw | ConvertFrom-Json } catch { continue }
            if ($info.schemaVersion -eq 1 -and $info.prxSha256 -eq $hash) { $matched = $info; $infoPath = $candidate.FullName; break }
        }
    }
}
$mode = if (-not $hash) { 'Missing PRX' } elseif (-not $matched) { 'Unknown build (no matching build information)' } elseif ($matched.runtimeManifest) { 'Experimental native mod loader' } elseif ($matched.lateScriptInjection) { 'Experimental late script injection' } elseif ($matched.filesystemOverrides) { 'Filesystem overrides only' } else { 'Bootstrap only (mods not loaded)' }
$modsRoot = Join-Path $GameRoot 'R2Northstar\mods'
$mods = @()
if (Test-Path -LiteralPath $modsRoot -PathType Container) {
    $mods = @(Get-ChildItem -LiteralPath $modsRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'mod.json') -PathType Leaf } | ForEach-Object Name)
}
$eboot = Join-Path $GameRoot 'eboot.bin'
$ebootHash = if (Test-Path -LiteralPath $eboot -PathType Leaf) { (Get-FileHash -LiteralPath $eboot -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
$bootstrap = if (-not $ebootHash) { 'Missing eboot' } elseif ($ebootHash -eq '590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824') { 'Retail eboot; bootstrap not enabled' } else { 'Non-retail eboot; bootstrap not verified by this check' }
[pscustomobject]@{
    GameRoot = $GameRoot
    RuntimeMode = $mode
    PrxSha256 = $hash
    BuildInfo = $infoPath
    Bootstrap = $bootstrap
    ModFolders = $mods
    ProfileSettingsPresent = Test-Path -LiteralPath (Join-Path $GameRoot 'R2Northstar\enabledmods.json') -PathType Leaf
    Authentication = if ($matched) { $matched.authentication } else { 'Unknown' }
    FullCompatibilityVerified = $false
    Note = 'Folder presence/build flags do not prove script execution. Check the current boot log for fatal script errors and UI lifecycle completion. Guest /data enabled settings override the app0 profile.'
}
