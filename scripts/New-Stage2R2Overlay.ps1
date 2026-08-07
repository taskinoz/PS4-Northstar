[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Manifest = (Join-Path $PSScriptRoot '..\config\stage2-overlay-manifest.json'),
    [switch] $Clean
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [System.IO.Path]::GetFullPath($Config)
$manifestPath = [System.IO.Path]::GetFullPath($Manifest)
& (Join-Path $PSScriptRoot 'Test-Environment.ps1') -Config $configPath | Out-Null
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Overlay manifest not found: $manifestPath" }
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$overlayManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$ps4GameRoot = if ([System.IO.Path]::IsPathRooted($settings.ps4GameRoot)) { [System.IO.Path]::GetFullPath($settings.ps4GameRoot) } else { throw "ps4GameRoot must be absolute in $configPath" }
if (-not (Test-Path -LiteralPath $ps4GameRoot -PathType Container)) { throw "PS4 game root not found: $ps4GameRoot" }
$overlayRoot = Join-Path $ps4GameRoot $overlayManifest.target
$metadataRoot = Join-Path $ps4GameRoot $overlayManifest.metadataTarget
$stagedRecordPath = Join-Path $overlayRoot '.ns_overlay_staged.json'

if ($Clean -and (Test-Path -LiteralPath $stagedRecordPath)) {
    if ($PSCmdlet.ShouldProcess($overlayRoot, 'Remove files previously staged by this overlay')) {
        $record = Get-Content -LiteralPath $stagedRecordPath -Raw | ConvertFrom-Json
        foreach ($entry in $record.staged) {
            $filePath = Join-Path $overlayRoot $entry
            if (Test-Path -LiteralPath $filePath -PathType Leaf) { Remove-Item -LiteralPath $filePath -Force }
        }
        foreach ($entry in $record.metadata) {
            $filePath = Join-Path $ps4GameRoot $entry
            if (Test-Path -LiteralPath $filePath -PathType Leaf) { Remove-Item -LiteralPath $filePath -Force }
        }
        foreach ($dir in (Get-ChildItem -LiteralPath $overlayRoot -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending)) {
            if (@($dir.GetFileSystemInfos()).Count -eq 0) { Remove-Item -LiteralPath $dir.FullName -Force }
        }
        if (Test-Path -LiteralPath $metadataRoot) {
            foreach ($dir in (Get-ChildItem -LiteralPath $metadataRoot -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending)) {
                if (@($dir.GetFileSystemInfos()).Count -eq 0) { Remove-Item -LiteralPath $dir.FullName -Force }
            }
        }
        Remove-Item -LiteralPath $stagedRecordPath -Force
    }
}

New-Item -ItemType Directory -Path $overlayRoot -Force | Out-Null
New-Item -ItemType Directory -Path $metadataRoot -Force | Out-Null
$excludePatterns = @($overlayManifest.exclude | ForEach-Object {
    '^' + [System.Text.RegularExpressions.Regex]::Escape($_).Replace('\*', '.*').Replace('\?', '.') + '$'
})
$staged = @()
$metadataStaged = @()
foreach ($mod in $overlayManifest.mods) {
    $source = Join-Path $settings.northstarModsRoot $mod.sourceSubdirectory
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Manifest source for '$($mod.name)' was not found: $source" }
    $files = Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {
        $name = $_.Name
        $excluded = $false
        foreach ($pattern in $excludePatterns) {
            if ($name -match $pattern) { $excluded = $true; break }
        }
        -not $excluded
    }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length + 1)
        $destination = Join-Path $overlayRoot $relative
        if ($PSCmdlet.ShouldProcess($destination, "Stage $($mod.name): $relative")) {
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            $staged += [pscustomobject]@{ Mod = $mod.name; Relative = $relative }
        }
    }
    $modJsonSource = Join-Path (Join-Path $settings.northstarModsRoot $mod.name) 'mod.json'
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
    [IO.File]::WriteAllLines($manifestDestination, @($overlayManifest.mods | ForEach-Object { $_.name }),
        [Text.UTF8Encoding]::new($false))
    $metadataStaged += $manifestRelative
}
[IO.File]::WriteAllText($stagedRecordPath,
    ([pscustomobject]@{
        version = 1
        staged = @($staged)
        metadata = @($metadataStaged)
    } | ConvertTo-Json -Depth 3))
[pscustomobject]@{
    Manifest = $manifestPath
    OverlayRoot = $overlayRoot
    MetadataRoot = $metadataRoot
    Mods = (($overlayManifest.mods | ForEach-Object { $_.name }) -join ', ')
    StagedFiles = $staged.Count
    MetadataFiles = $metadataStaged.Count
}
