[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Manifest = (Join-Path $PSScriptRoot '..\config\stage2-overlay-manifest.json'),
    [switch] $Clean,
    [switch] $SkipR2ModStage
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [System.IO.Path]::GetFullPath($Config)
$manifestPath = [System.IO.Path]::GetFullPath($Manifest)
& (Join-Path $PSScriptRoot 'Test-Environment.ps1') -Config $configPath | Out-Null
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Overlay manifest not found: $manifestPath" }
. (Join-Path $PSScriptRoot 'Resolve-ModSource.ps1')
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$overlayManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$ps4GameRoot = if ([System.IO.Path]::IsPathRooted($settings.ps4GameRoot)) { [System.IO.Path]::GetFullPath($settings.ps4GameRoot) } else { throw "ps4GameRoot must be absolute in $configPath" }
if (-not (Test-Path -LiteralPath $ps4GameRoot -PathType Container)) { throw "PS4 game root not found: $ps4GameRoot" }
$overlayRoot = Join-Path $ps4GameRoot $overlayManifest.target
$modsRoot = Join-Path $ps4GameRoot $overlayManifest.metadataTarget
$stagedRecordPath = Join-Path $overlayRoot '.ns_overlay_staged.json'
$excludePatterns = @($overlayManifest.exclude | ForEach-Object {
    '^' + [System.Text.RegularExpressions.Regex]::Escape($_).Replace('\*', '.*').Replace('\?', '.') + '$'
})
function Test-Excluded([string] $Name) {
    foreach ($pattern in $excludePatterns) {
        if ($Name -match $pattern) { return $true }
    }
    return $false
}

if ($Clean -and (Test-Path -LiteralPath $stagedRecordPath)) {
    if ($PSCmdlet.ShouldProcess($ps4GameRoot, 'Remove files previously staged by this overlay')) {
        $record = Get-Content -LiteralPath $stagedRecordPath -Raw | ConvertFrom-Json
        foreach ($entry in $record.staged) {
            $filePath = Join-Path $overlayRoot $entry
            if (Test-Path -LiteralPath $filePath -PathType Leaf) { Remove-Item -LiteralPath $filePath -Force }
        }
        foreach ($entry in @($record.metadata) + @($record.modsMirror)) {
            $filePath = Join-Path $ps4GameRoot $entry
            if (Test-Path -LiteralPath $filePath -PathType Leaf) { Remove-Item -LiteralPath $filePath -Force }
        }
        foreach ($dir in (Get-ChildItem -LiteralPath $overlayRoot -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending)) {
            if (@($dir.GetFileSystemInfos()).Count -eq 0) { Remove-Item -LiteralPath $dir.FullName -Force }
        }
        foreach ($dir in (Get-ChildItem -LiteralPath $modsRoot -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending)) {
            if (@($dir.GetFileSystemInfos()).Count -eq 0) { Remove-Item -LiteralPath $dir.FullName -Force }
        }
        Remove-Item -LiteralPath $stagedRecordPath -Force
    }
}

New-Item -ItemType Directory -Path $overlayRoot -Force | Out-Null
New-Item -ItemType Directory -Path $modsRoot -Force | Out-Null
$staged = @()
$metadataStaged = @()
$modsMirror = @()
foreach ($mod in $overlayManifest.mods) {
    $source = Get-ModSourceDir -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Manifest source for '$($mod.name)' was not found: $source" }

    # Step 1: mirror the whole PC mod dir into /app0/mods/<ModName> so the
    # PS4 layout matches the PC mod layout exactly (mod.json, mod/, ...).
    $modsDestination = Join-Path $modsRoot $mod.name
    $files = Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object { -not (Test-Excluded $_.Name) }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length + 1)
        $destination = Join-Path $modsDestination $relative
        if ($PSCmdlet.ShouldProcess($destination, "Mirror $($mod.name): $relative")) {
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            $modsMirror += (Join-Path $overlayManifest.metadataTarget (Join-Path $mod.name $relative))
        }
    }

    # Step 2: generate the r2 engine overlay from the mod's 'mod' subdirectory.
    # r2 is the engine GAME search root, so only the content that PC serves
    # from MOD_OVERRIDE_DIR (the mod/ dir) goes there. Read it from the source
    # so r2 generation does not depend on the just-written mirror.
    # -SkipR2ModStage: mods load from their own /app0/mods/<Name>/mod dir via
    # the runtime search-path overlay instead, so nothing is dumped into r2.
    if ($SkipR2ModStage) {
        Write-Host "SkipR2ModStage: mod content stays in /app0/mods (r2 mod overlay skipped) for $($mod.name)"
        continue
    }
    $modContent = Join-Path $source 'mod'
    if (-not (Test-Path -LiteralPath $modContent -PathType Container)) {
        Write-Warning "$($mod.name): no 'mod' subdirectory; no r2 overlay staged"
        continue
    }
    $contentFiles = Get-ChildItem -LiteralPath $modContent -Recurse -File | Where-Object { -not (Test-Excluded $_.Name) }
    foreach ($file in $contentFiles) {
        $relative = $file.FullName.Substring($modContent.Length + 1)
        $destination = Join-Path $overlayRoot $relative
        if ($PSCmdlet.ShouldProcess($destination, "Stage $($mod.name): $relative")) {
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            $staged += [pscustomobject]@{ Mod = $mod.name; Relative = $relative }
        }
    }
}

$modNames = @($overlayManifest.mods | ForEach-Object { $_.name })
foreach ($mod in $overlayManifest.mods) {
    $modJsonSource = Get-ModJsonPath -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot
    if (Test-Path -LiteralPath $modJsonSource -PathType Leaf) {
        $metadataRelative = Join-Path $overlayManifest.metadataTarget (Join-Path $mod.name 'mod.json')
        $metadataDestination = Join-Path $ps4GameRoot $metadataRelative
        if ($PSCmdlet.ShouldProcess($metadataDestination, "Publish metadata $($mod.name): mod.json")) {
            New-Item -ItemType Directory -Path (Split-Path $metadataDestination -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $modJsonSource -Destination $metadataDestination -Force
            $metadataStaged += $metadataRelative
        }
    }
}
$manifestRelative = Join-Path $overlayManifest.metadataTarget '.ns_mod_manifest'
$manifestDestination = Join-Path $ps4GameRoot $manifestRelative
if ($PSCmdlet.ShouldProcess($manifestDestination, 'Write mod manifest')) {
    [IO.File]::WriteAllLines($manifestDestination, $modNames,
        [Text.UTF8Encoding]::new($false))
    $metadataStaged += $manifestRelative
}
[IO.File]::WriteAllText($stagedRecordPath,
    ([pscustomobject]@{
        version = 2
        staged = @($staged)
        metadata = @($metadataStaged)
        modsMirror = @($modsMirror)
    } | ConvertTo-Json -Depth 3))
[pscustomobject]@{
    Manifest = $manifestPath
    OverlayRoot = $overlayRoot
    ModsRoot = $modsRoot
    Mods = ($modNames -join ', ')
    StagedFiles = $staged.Count
    ModsMirrorFiles = $modsMirror.Count
    MetadataFiles = $metadataStaged.Count
}
