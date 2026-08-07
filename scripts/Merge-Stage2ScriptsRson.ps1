[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $OverlayManifest = (Join-Path $PSScriptRoot '..\config\stage2-overlay-manifest.json'),
    [string] $VanillaScriptsRson = (Join-Path $PSScriptRoot '..\tools\vanilla-scripts\scripts\vscripts\scripts.rson'),
    [switch] $Probe
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [System.IO.Path]::GetFullPath($Config)
$manifestPath = [System.IO.Path]::GetFullPath($OverlayManifest)
$vanillaPath = [System.IO.Path]::GetFullPath($VanillaScriptsRson)
foreach ($required in @($configPath, $manifestPath, $vanillaPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file not found: $required" }
}
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$ps4GameRoot = if ([System.IO.Path]::IsPathRooted($settings.ps4GameRoot)) { [System.IO.Path]::GetFullPath($settings.ps4GameRoot) } else { throw "ps4GameRoot must be absolute in $configPath" }
if (-not (Test-Path -LiteralPath $ps4GameRoot -PathType Container)) { throw "PS4 game root not found: $ps4GameRoot" }
$overlayRoot = Join-Path $ps4GameRoot $manifestData.target
$stagedRecordPath = Join-Path $overlayRoot '.ns_overlay_staged.json'

$scriptRoot = Join-Path $overlayRoot 'scripts\vscripts'
$rsonRelative = 'scripts\vscripts\scripts.rson'
$rsonDestination = Join-Path $overlayRoot $rsonRelative
$probeRelative = 'scripts\vscripts\ns_m6_probe.gnut'
$probeDestination = Join-Path $overlayRoot $probeRelative
$probeMarker = '// NS M6 script-injection probe. Only staged during M6 iterations (never in the shipping overlay).'

$crlf = "`r`n"
$blocks = [System.Collections.Generic.List[string]]::new()
$skipped = @()
$emitted = 0

foreach ($mod in $manifestData.mods) {
    $modJsonPath = Join-Path $settings.northstarModsRoot (Join-Path $mod.name 'mod.json')
    if (-not (Test-Path -LiteralPath $modJsonPath -PathType Leaf)) { throw "mod.json not found for '$($mod.name)': $modJsonPath" }
    $modJson = Get-Content -LiteralPath $modJsonPath -Raw | ConvertFrom-Json

    foreach ($script in $modJson.Scripts) {
        $path = if ($script.PSObject.Properties.Name -contains 'Path') { $script.Path } else { $script }
        $runOn = if ($script.PSObject.Properties.Name -contains 'RunOn') { $script.RunOn } else { $null }
        if ([string]::IsNullOrWhiteSpace($runOn)) {
            Write-Warning "$($mod.name): script '$path' has no RunOn; skipped"
            continue
        }
        $staged = Join-Path $scriptRoot $path
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) {
            Write-Warning "$($mod.name): script '$path' not staged in r2 (missing file: $staged); skipped"
            $skipped += $path
            continue
        }
        $blocks.Add("When: `"$runOn`"$crlf`Scripts:$crlf[$crlf`t$path$crlf]$crlf")
        $emitted++
    }

    $initScript = $modJson.PSObject.Properties.Name -contains 'InitScript' ? $modJson.InitScript : $null
    foreach ($init in @($initScript)) {
        if ([string]::IsNullOrWhiteSpace($init)) { continue }
        $staged = Join-Path $scriptRoot $init
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) {
            Write-Warning "$($mod.name): InitScript '$init' not staged in r2 (missing file: $staged); skipped"
            $skipped += $init
            continue
        }
        $blocks.Add("When: `"CLIENT`"$crlf`Scripts:$crlf[$crlf`t$init$crlf]$crlf")
        $emitted++
    }
}

if ($Probe) {
    # A bare call at file scope does NOT compile here: the UI script compiler
    # rejects it ("Global variable definition is followed by '('. Did you
    # forget to declare it as a 'function'?", verified run 20260807-143020;
    # matches the older "Only functions, consts, or global variables are
    # allowed at file scope" error from run 20260806-205502). Only
    # function/const/global-variable declarations are legal at file scope in
    # this compiler, so proving load-time execution requires either a
    # RunOn/InitScript-style callback the engine itself calls after compiling
    # (see mod.json InitScript, already parsed in runtime.cpp) or a working
    # name-resolution probe — not a top-level statement. See
    # docs/TECHNICAL-NOTES.md and docs/GOALS.md (Goal 6) for the current status.
    $probeBody = @(
        $probeMarker
        'const NSM6_PROBE_MARKER = "[NorthstarPS4] M6 script probe: UI VM compiled and executed ns_m6_probe.gnut"'
        ''
        'void function NSM6ProbeMarker()'
        '{'
        "`tprintt(NSM6_PROBE_MARKER)"
        '}'
    ) -join $crlf
    if ($PSCmdlet.ShouldProcess($probeDestination, "Stage probe script: $probeRelative")) {
        [IO.File]::WriteAllText($probeDestination, $probeBody + $crlf, [Text.UTF8Encoding]::new($false))
    }
    $blocks.Add("When: `"UI`"$crlf`Scripts:$crlf[$crlf`tns_m6_probe.gnut$crlf]$crlf")
    $emitted++
}

if ($emitted -eq 0) {
    throw "No mod scripts were eligible for merge; nothing to write."
}

$vanilla = [IO.File]::ReadAllText($vanillaPath)
$marker = '// DEVSCRIPTS CONTENT'
$markerIndex = $vanilla.LastIndexOf($marker)
$insertIndex = if ($markerIndex -ge 0) { $markerIndex } else { $vanilla.Length }
$merged = $vanilla.Substring(0, $insertIndex) +
    ($blocks -join $crlf) +
    $vanilla.Substring($insertIndex)

if ($PSCmdlet.ShouldProcess($rsonDestination, "Write merged scripts.rson ($emitted blocks)")) {
    New-Item -ItemType Directory -Path $scriptRoot -Force | Out-Null
    [IO.File]::WriteAllText($rsonDestination, $merged, [Text.UTF8Encoding]::new($false))
}

if (Test-Path -LiteralPath $stagedRecordPath -PathType Leaf) {
    $record = Get-Content -LiteralPath $stagedRecordPath -Raw | ConvertFrom-Json
    foreach ($entry in @($rsonRelative, $(if ($Probe) { $probeRelative }))) {
        $already = @($record.staged | ForEach-Object { $_.Relative }) -contains $entry
        if (-not $already) { $record.staged += [pscustomobject]@{ Mod = 'M6.ScriptsRson'; Relative = $entry } }
    }
    [IO.File]::WriteAllText($stagedRecordPath, ($record | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
}

[pscustomobject]@{
    Vanilla = $vanillaPath
    Destination = $rsonDestination
    Mods = (($manifestData.mods | ForEach-Object { $_.name }) -join ', ')
    BlocksAppended = $emitted
    SkippedMissing = $skipped.Count
    Probe = [bool]$Probe
}
