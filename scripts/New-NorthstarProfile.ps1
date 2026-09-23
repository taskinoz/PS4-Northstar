[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Output = (Join-Path $PSScriptRoot '..\dist\northstar-profile'),
    [switch] $IncludePs4CompatibilityMods,
    [switch] $IncludeAIHarness
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$settings = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$source = [IO.Path]::GetFullPath($settings.northstarModsRoot)
$outputRoot = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Mod source not found: $source" }
# A fresh output prevents deleted/disabled source files lingering in a rebuilt package.
# Existing profiles may contain user mods/settings and are never cleared implicitly.
if (Test-Path -LiteralPath $outputRoot) { throw "Output already exists: $outputRoot. Choose a fresh -Output directory." }
$profile = Join-Path $outputRoot 'R2Northstar'
$sources = @($source)
if ($IncludePs4CompatibilityMods -or $IncludeAIHarness) { $sources += Join-Path $repositoryRoot 'mods' }
$folders = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$plan = [Collections.Generic.List[object]]::new()
foreach ($modRoot in $sources) {
    foreach ($directory in (Get-ChildItem -LiteralPath $modRoot -Directory | Sort-Object Name)) {
        if ($directory.Name -eq 'AI.Harness' -and -not $IncludeAIHarness) { continue }
        if ($modRoot -eq (Join-Path $repositoryRoot 'mods') -and -not $IncludePs4CompatibilityMods -and $directory.Name -ne 'AI.Harness') { continue }
        $metadata = Join-Path $directory.FullName 'mod.json'
        if (-not (Test-Path -LiteralPath $metadata -PathType Leaf)) { continue }
        if (-not $folders.Add($directory.Name)) { throw "Duplicate mod folder: $($directory.Name)" }
        if ($directory.Name.Length -ge 64 -or $directory.Name.StartsWith('.')) { throw "Unsupported mod folder name: $($directory.Name)" }
        $mod = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
        if ($mod.Name -isnot [string] -or [string]::IsNullOrWhiteSpace($mod.Name) -or $mod.Name.Length -ge 64) { throw "Invalid mod Name: $metadata" }
        # Copy all mod content byte-for-byte, including metadata, keyvalues and assets.
        # Native PC plugins belong to the platform runtime and cannot execute on PS4.
        $items = @(Get-Item -LiteralPath $directory.FullName) + @(Get-ChildItem -LiteralPath $directory.FullName -Recurse -Force)
        foreach ($item in $items) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Mod contains a link; supply regular files: $($item.FullName)" }
        }
        $plan.Add([pscustomobject]@{ Folder = $directory.Name; Source = $directory.FullName; Name = $mod.Name; Files = @($items | Where-Object { -not $_.PSIsContainer }) })
    }
}
if ($plan.Count -eq 0) { throw "No mod.json folders found in $source" }
if ($plan.Count -gt 128) { throw 'PS4 catalog currently supports at most 128 mod folders.' }
$enabledSource = Join-Path (Split-Path $source -Parent) 'enabledmods.json'
if (Test-Path -LiteralPath $enabledSource -PathType Leaf) {
    $enabled = Get-Content -LiteralPath $enabledSource -Raw | ConvertFrom-Json
    if ($enabled -isnot [pscustomobject]) { throw "Expected enabledmods.json object: $enabledSource" }
}
if ($PSCmdlet.ShouldProcess($outputRoot, 'Create PC-layout Northstar profile (no game archive changes)')) {
    $modsDestination = Join-Path $profile 'mods'
    New-Item -ItemType Directory -Path $modsDestination -Force | Out-Null
    $hashes = [Collections.Generic.List[object]]::new()
    foreach ($mod in $plan) {
        foreach ($file in $mod.Files) {
            $relative = $file.FullName.Substring($mod.Source.Length + 1)
            $destination = Join-Path (Join-Path $modsDestination $mod.Folder) $relative
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination
            $expected = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expected) { throw "Copy verification failed: $destination" }
            $hashes.Add([pscustomobject]@{ Path = "mods/$($mod.Folder)/$($relative.Replace('\', '/'))"; SHA256 = $expected })
        }
    }
    if (Test-Path -LiteralPath $enabledSource -PathType Leaf) {
        Copy-Item -LiteralPath $enabledSource -Destination (Join-Path $profile 'enabledmods.json')
    } else {
        [IO.File]::WriteAllText((Join-Path $profile 'enabledmods.json'), '{"Version":1}', [Text.UTF8Encoding]::new($false))
    }
    # Only used when the emulator cannot enumerate directories; not a mod allowlist.
    [IO.File]::WriteAllLines((Join-Path $modsDestination '.ns_mod_manifest'), [string[]]@($plan.Folder), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $outputRoot 'profile-files.json'), ($hashes | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
}
[pscustomobject]@{ Profile = $profile; Mods = $plan.Count; Files = ($plan.Files | Measure-Object).Count; RuntimeScriptLoading = 'Experimental; PS4 lifecycle hooks incomplete' }
