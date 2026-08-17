[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Manifest = (Join-Path $PSScriptRoot '..\config\stage1-manifest.json'),
    [string] $VanillaScriptsRson = (Join-Path $PSScriptRoot '..\tools\vanilla-scripts\scripts\vscripts\scripts.rson'),
    [string] $SparseRoot = (Join-Path $PSScriptRoot '..\work\stage1\sparse'),
    # Restrict to a subset of the manifest's mods (by Name). Omit to process
    # every mod in the manifest. Real Northstar.Client/Northstar.Custom are
    # much larger and not yet vetted through this pipeline (Goal: task 4) --
    # pass -ModNames explicitly until that's done, don't rely on the default.
    [string[]] $ModNames,
    # Script Path values (as written in mod.json, e.g. "ui/menu_ns_modmenu.nut")
    # to skip entirely, from any mod. For deferring real-Northstar features
    # that need native support this port doesn't have yet, rather than
    # stubbing every native call one at a time. Stubbing those natives would
    # make the menus *open* without being honestly non-functional (fake
    # data, buttons that silently do nothing) -- excluding the files
    # entirely until the real native support lands is more honest. Their
    # Before/After hooks (if any) are skipped too. Defaults to the exact set
    # verified live 2026-08-15 to boot clean (no FatalError) with real
    # Northstar.Client -- re-run with -ExcludeScripts @() (or a shorter
    # list) to re-attempt any of these once its native dependency exists:
    #   - ui/menu_ns_modmenu.nut (in-game mod list): NSSetModEnabled,
    #     NSGetModsInformation, NSReloadMods, NSGetModInformation -- needs
    #     Goal 7 (mod enable/disable + lifecycle).
    #   - ui/menu_ns_moddownload.nut (mod downloading): NSDownloadMod,
    #     NSCancelModDownload, NSFetchVerifiedModsManifesto,
    #     NSGetModInstallState -- needs Goal 7 plus an online mod repository
    #     path, likely out of scope entirely for this port.
    #   - ui/menu_ns_setversionlabel.nut (cosmetic version label): needs
    #     NSGetModInformation (see above) plus NS_VERSION_* native constants.
    #   - ui/menu_ns_serverbrowser.nut: needs the native masterserver client
    #     (Task 9) for NSRequestServerList/NSGetGameServers/etc., AND has an
    #     unresolved compile error of its own ("wrong number of parameters
    #     (calling unknown function): found 5, expected 4") not yet
    #     root-caused -- revisit when implementing Task 9, with full context
    #     of exactly which native call site it is.
    #   - ui/menu_ns_connect_password.nut: depends on
    #     menu_ns_serverbrowser.nut's OnServerSelected_Threaded; needs Task 9
    #     as well as the above fixed first.
    #   - ui/menu_ns_custom_match_settings.nut,
    #     ui/menu_ns_custom_match_settings_categories.nut: need
    #     GetPrivateMatchSettingCategories / GetPrivateMatchCustomSettingsForCategory
    #     -- the same "CustomServers API" category Apply-Stage1Compatibility.ps1
    #     already strips from menu_private_match.nut for the same reason.
    #   - ui/atlas_auth.nut: needs NSIsMasterServerAuthenticated and the rest
    #     of the Atlas auth native surface -- needs Task 9.
    [string[]] $ExcludeScripts = @(
        'ui/menu_ns_modmenu.nut',
        'ui/menu_ns_moddownload.nut',
        'ui/menu_ns_setversionlabel.nut',
        'ui/menu_ns_serverbrowser.nut',
        'ui/menu_ns_connect_password.nut',
        'ui/menu_ns_custom_match_settings.nut',
        'ui/menu_ns_custom_match_settings_categories.nut',
        'ui/atlas_auth.nut'
    )
)

# Bakes each mod's Scripts[]/UICallback/ClientCallback declarations into the
# Stage 1 VPK-repack sparse tree, as a real, content-editable scripts.rson
# entry -- replacing the two dead ends discovered 2026-08-08/2026-08-14:
#   1. A new .nut file is NOT automatically compiled just by sitting in the
#      right VPK directory (the "menu_direct_connect.nut never compiled"
#      bug) -- it needs a scripts.rson entry.
#   2. A modified scripts.rson staged into the r2 loose-file overlay is
#      ignored -- the engine reads the VPK-packed one whenever a path
#      exists in both. So the merged scripts.rson must be *baked into the
#      VPK* (this sparse tree), not r2-overlaid.
# This script handles (1) and (2). It does NOT implement NorthstarLauncher's
# native UICallback/ClientCallback hook dispatch (there is no C++ hook point
# on this port) -- see Generate-ModHookDispatch.ps1 for the companion piece
# that turns each mod's declared Before/After hooks into real Squirrel calls
# inserted at a proven-compiled anchor point.
#
# Known simplification (fine for UI-only/Localisation-only mods like
# Northstar.DirectConnect and Northstar.PS4; revisit before running this
# against Northstar.Client/Custom, which span many more RunOn contexts):
# a mod's entire `mod/` directory is mirrored into every VPK target implied
# by its Scripts[] RunOn values (RunOn containing "UI" -> frontend; RunOn
# containing "CLIENT" or "SERVER" -> mp_common). This can duplicate content
# across VPKs but is always correctness-safe.

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [System.IO.Path]::GetFullPath($Config)
$manifestPath = [System.IO.Path]::GetFullPath($Manifest)
$vanillaPath = [System.IO.Path]::GetFullPath($VanillaScriptsRson)
$sparseRoot = [System.IO.Path]::GetFullPath($SparseRoot)
foreach ($required in @($configPath, $manifestPath, $vanillaPath, $sparseRoot)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required path not found: $required" }
}
. (Join-Path $PSScriptRoot 'Resolve-ModSource.ps1')
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$crlf = "`r`n"

$mods = $manifestData.mods
if ($ModNames) { $mods = $mods | Where-Object { $_.name -in $ModNames } }
if (-not $mods) { throw "No mods selected (manifest has $(@($manifestData.mods).Count), filter matched 0)." }

function Get-VpkTargetsForRunOn([string] $RunOn) {
    $targets = [System.Collections.Generic.List[string]]::new()
    if ($RunOn -match 'UI') { $targets.Add('frontend') }
    if ($RunOn -match 'CLIENT|SERVER') { $targets.Add('mp_common') }
    if ($targets.Count -eq 0) { throw "Don't know which VPK target serves RunOn '$RunOn' -- extend Get-VpkTargetsForRunOn." }
    return $targets
}

# [ordered] so target/block insertion order is deterministic -- InitScript
# blocks must land before a mod's other Scripts[] blocks in the final merged
# file (see the InitScript comment below), which depends on iteration order
# being exactly insertion order, not hashtable-implementation-defined.
$rsonBlocksByTarget = [ordered]@{ frontend = [System.Collections.Generic.List[string]]::new() }
# Dedup key ("target|runOn|path") -> $true. Two mods can legitimately declare
# the same script path (e.g. Northstar.PS4 overriding one of Northstar.Client's
# files) -- the file copy step below already makes the later-processed mod's
# content win, but scripts.rson must still only list the path once per
# target/RunOn or the engine would try to compile it twice.
$seenRsonEntries = @{}
$hooks = [System.Collections.Generic.List[pscustomobject]]::new()
$copiedTargets = [System.Collections.Generic.HashSet[string]]::new()
$report = [System.Collections.Generic.List[pscustomobject]]::new()

# Process in LoadPriority order (ties broken by name) so a later/override mod
# (e.g. Northstar.PS4, LoadPriority 99) is staged after -- and so wins file
# collisions against -- an earlier one (e.g. Northstar.Client, LoadPriority 0).
# LoadPriority itself lives in each mod's own mod.json, not the manifest
# entry, hence this pre-pass.
$modsWithPriority = foreach ($mod in $mods) {
    $priority = 0
    try {
        $mj = Get-Content -LiteralPath (Get-ModJsonPath -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot) -Raw | ConvertFrom-Json
        if ($mj.PSObject.Properties.Name -contains 'LoadPriority') { $priority = [int]$mj.LoadPriority }
    } catch {}
    [pscustomobject]@{ Mod = $mod; Priority = $priority }
}
foreach ($entry in ($modsWithPriority | Sort-Object Priority, { $_.Mod.name })) {
    $mod = $entry.Mod
    $modSourceDir = Get-ModSourceDir -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot
    $modJsonPath = Get-ModJsonPath -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot
    if (-not (Test-Path -LiteralPath $modJsonPath -PathType Leaf)) { throw "mod.json not found for '$($mod.name)': $modJsonPath" }
    $modJson = Get-Content -LiteralPath $modJsonPath -Raw | ConvertFrom-Json
    $modContentDir = Join-Path $modSourceDir 'mod'
    if (-not (Test-Path -LiteralPath $modContentDir -PathType Container)) { throw "Mod content dir not found for '$($mod.name)': $modContentDir" }

    # Files to copy, per VPK target: EXACT relative paths under mod/ only --
    # never the whole mod/ tree. Real PC Northstar mods routinely ship their
    # own copies of core files (panel_mainmenu.nut, _menus.nut, menu_main.nut,
    # ...) as content overrides via PC's r2 search-path overlay, a mechanism
    # this project doesn't use for VPK-baked content. A wholesale copy here
    # silently clobbers this port's PS4-specific patches to those exact
    # files with upstream PC content -- confirmed live 2026-08-14: it wiped
    # the Direct Connect button out of panel_mainmenu.nut, the mod hook
    # dispatch markers out of _menus.nut, and the AddServerToClientStringCommandCallback
    # stub out of _custom_codecallbacks_client.gnut, all silently (no error,
    # since Copy-Item -Force just overwrites). Only ever stage files this
    # mod's own mod.json explicitly declares (Scripts[]/InitScript/Localisation[]).
    $filesByTarget = @{}
    function Add-StagedFile([string] $Target, [string] $RelativePath) {
        if (-not $filesByTarget.ContainsKey($Target)) { $filesByTarget[$Target] = [System.Collections.Generic.List[string]]::new() }
        if (-not $filesByTarget[$Target].Contains($RelativePath)) { $filesByTarget[$Target].Add($RelativePath) }
    }

    # InitScript is staged and listed in scripts.rson BEFORE this mod's own
    # Scripts[] entries (not after -- confirmed live 2026-08-15: adding it
    # after still hit the same ModInfo error, because scripts.rson blocks
    # compile in file order and a struct type must be compiled before
    # anything that uses it as a type annotation; Squirrel has no forward
    # declaration for struct types the way `global function X` forward-
    # declares functions). It's compiled into every VM context the mod has
    # any script for (UI and CLIENT here), not just CLIENT -- Northstar.Client's
    # InitScript (cl_northstar_client_init.nut) defines `global struct
    # ModInfo`, which its own UI script menu_ns_modmenu.nut uses as a type,
    # and Squirrel VMs per context don't share a global table.
    $initScript = if ($modJson.PSObject.Properties.Name -contains 'InitScript') { $modJson.InitScript } else { $null }
    if (-not [string]::IsNullOrWhiteSpace($initScript)) {
        $relative = Join-Path 'scripts\vscripts' $initScript
        $staged = Join-Path $modContentDir $relative
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) { throw "$($mod.name): InitScript '$initScript' not found at $staged" }
        $initRunOn = 'UI || CLIENT'
        foreach ($target in (Get-VpkTargetsForRunOn $initRunOn)) { Add-StagedFile $target $relative }
        $dedupKey = "$initRunOn|$initScript"
        if (-not $seenRsonEntries.ContainsKey($dedupKey)) {
            $seenRsonEntries[$dedupKey] = $true
            if (-not $rsonBlocksByTarget.Contains('frontend')) { $rsonBlocksByTarget['frontend'] = [System.Collections.Generic.List[string]]::new() }
            $rsonBlocksByTarget['frontend'].Add("When: `"$initRunOn`"$crlf`Scripts:$crlf[$crlf`t$initScript$crlf]$crlf")
        }
    }

    $scripts = if ($modJson.PSObject.Properties.Name -contains 'Scripts' -and $modJson.Scripts) { @($modJson.Scripts) } else { @() }
    foreach ($script in $scripts) {
        $path = if ($script.PSObject.Properties.Name -contains 'Path') { $script.Path } else { $script }
        if ($ExcludeScripts -contains $path) { Write-Verbose "$($mod.name): script '$path' explicitly excluded; skipped"; continue }
        $runOn = if ($script.PSObject.Properties.Name -contains 'RunOn') { $script.RunOn } else { $null }
        if ([string]::IsNullOrWhiteSpace($runOn)) { Write-Warning "$($mod.name): script '$path' has no RunOn; skipped"; continue }
        $relative = Join-Path 'scripts\vscripts' $path
        $staged = Join-Path $modContentDir $relative
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) { throw "$($mod.name): script '$path' not found at $staged" }

        foreach ($target in (Get-VpkTargetsForRunOn $runOn)) {
            Add-StagedFile $target $relative
            # Keyed by RunOn+path only, NOT target: a script whose RunOn
            # matches multiple VPK targets (e.g. "UI || CLIENT" -> both
            # frontend and mp_common) gets staged to each target's file tree,
            # but every target's blocks are baked into the SAME single
            # frontend-resident scripts.rson -- listing it once per target
            # would list the same path twice in one file. Confirmed live
            # 2026-08-14: "FatalError: Script "ui/menu_ns_color_picker.nut"
            # is being loaded more than once from "scripts/vscripts/scripts.rson""
            # (its RunOn is "UI || CLIENT").
            $dedupKey = "$runOn|$path"
            if (-not $seenRsonEntries.ContainsKey($dedupKey)) {
                $seenRsonEntries[$dedupKey] = $true
                if (-not $rsonBlocksByTarget.Contains($target)) { $rsonBlocksByTarget[$target] = [System.Collections.Generic.List[string]]::new() }
                $rsonBlocksByTarget[$target].Add("When: `"$runOn`"$crlf`Scripts:$crlf[$crlf`t$path$crlf]$crlf")
            }
        }

        foreach ($hookKind in @('UICallback', 'ClientCallback')) {
            if ($script.PSObject.Properties.Name -contains $hookKind -and $script.$hookKind) {
                foreach ($phase in @('Before', 'After')) {
                    if ($script.$hookKind.PSObject.Properties.Name -contains $phase -and $script.$hookKind.$phase) {
                        $hooks.Add([pscustomobject]@{
                            Mod = $mod.name
                            LoadPriority = if ($modJson.PSObject.Properties.Name -contains 'LoadPriority') { [int]$modJson.LoadPriority } else { 0 }
                            Kind = $hookKind
                            Phase = $phase
                            Function = $script.$hookKind.$phase
                        })
                    }
                }
            }
        }
    }

    $localisation = if ($modJson.PSObject.Properties.Name -contains 'Localisation' -and $modJson.Localisation) { @($modJson.Localisation) } else { @() }
    foreach ($locPattern in $localisation) {
        foreach ($language in @('english')) {
            # Only the language(s) this port ships are staged; extend this
            # list if/when other languages are supported.
            $locPath = $locPattern -replace '%language%', $language
            $relative = $locPath -replace '/', '\'
            $staged = Join-Path $modContentDir $relative
            if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) { Write-Warning "$($mod.name): Localisation '$locPath' not found at $staged; skipped"; continue }
            Add-StagedFile 'frontend' $relative
        }
    }

    if ($filesByTarget.Keys.Count -eq 0) { Write-Warning "$($mod.name): no Scripts[]/InitScript/Localisation[] entries; nothing to stage"; continue }

    $totalFiles = 0
    foreach ($target in $filesByTarget.Keys) {
        $destination = Join-Path $sparseRoot $target
        foreach ($relative in $filesByTarget[$target]) {
            $source = Join-Path $modContentDir $relative
            $destFile = Join-Path $destination $relative
            if ($PSCmdlet.ShouldProcess($destFile, "Stage $($mod.name): $relative")) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $destFile) -Force | Out-Null
                Copy-Item -LiteralPath $source -Destination $destFile -Force
            }
            $totalFiles++
        }
        [void]$copiedTargets.Add($target)
    }
    $report.Add([pscustomobject]@{ Mod = $mod.name; Targets = ($filesByTarget.Keys -join ', '); FilesStaged = $totalFiles; Hooks = @($hooks | Where-Object { $_.Mod -eq $mod.name }).Count })
}

# Bake the merged scripts.rson for the frontend VPK (the only VPK with a UI
# script VM; mp_common's CLIENT/SERVER scripts are covered by the same
# single scripts.rson -- verified: sh_damage_types.nut, a CLIENT-context
# mp_common file, is listed in this exact frontend-resident scripts.rson).
$vanilla = [System.IO.File]::ReadAllText($vanillaPath)
# Insert mod blocks right before the FIRST vanilla "When:" block (i.e. right
# after the header comment), not before '// DEVSCRIPTS CONTENT' near the end
# of the file. Squirrel has no forward declaration for types/structs, and a
# not-yet-compiled global referenced by an earlier-compiled file fails as
# "Undefined variable" rather than resolving lazily -- vanilla content early
# in this file (e.g. sh_damage_types.nut, ~line 191) can reference
# mod-provided globals (e.g. AddServerToClientStringCommandCallback from
# Northstar.Client/Northstar.PS4), so mod blocks must compile before ANY
# vanilla block that might depend on them. Each generated block is a
# self-contained "When: ... Scripts: [...]" unit, so relocating the whole
# group to the top is syntactically safe.
$firstWhenMatch = [System.Text.RegularExpressions.Regex]::Match($vanilla, '(?m)^When:')
$insertIndex = if ($firstWhenMatch.Success) { $firstWhenMatch.Index } else { $vanilla.Length }
$allBlocks = @()
foreach ($target in $rsonBlocksByTarget.Keys) { $allBlocks += $rsonBlocksByTarget[$target] }
if ($allBlocks.Count -eq 0) { throw "No scripts.rson blocks were generated; nothing to merge." }
$merged = $vanilla.Substring(0, $insertIndex) + ($allBlocks -join $crlf) + $crlf + $vanilla.Substring($insertIndex)
$rsonDestination = Join-Path $sparseRoot 'frontend\scripts\vscripts\scripts.rson'
if ($PSCmdlet.ShouldProcess($rsonDestination, "Write merged scripts.rson ($($allBlocks.Count) blocks)")) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $rsonDestination) -Force | Out-Null
    [System.IO.File]::WriteAllText($rsonDestination, $merged, $utf8NoBom)
}

# Regenerate the PS4 mod hook dispatch block in _menus.nut: real Northstar
# mods register UI additions via mod.json Scripts[].UICallback.Before/After,
# normally invoked by NorthstarLauncher's native C++ hook into the engine's
# UI init. No equivalent native hook point exists on this port, so this
# substitutes a generated, idempotent call list inserted between marker
# comments in InitMenus() -- Before hooks ahead of the vanilla AddMenu()
# calls, After hooks at the very end. ClientCallback hooks are not yet
# dispatched anywhere (no proven-compiled CLIENT-context anchor point has
# been wired up for this); a mod that only uses UICallback, like
# Northstar.DirectConnect, works fully today.
$uiHooks = @($hooks | Where-Object { $_.Kind -eq 'UICallback' } | Sort-Object LoadPriority, Mod)
$menusPath = Join-Path $sparseRoot 'frontend\scripts\vscripts\ui\_menus.nut'
if (-not (Test-Path -LiteralPath $menusPath -PathType Leaf)) { throw "_menus.nut not found: $menusPath" }
$menusContent = [System.IO.File]::ReadAllText($menusPath)

function Set-MarkerBlock([string] $Content, [string] $StartMarker, [string] $EndMarker, [string[]] $Lines) {
    $pattern = [regex]::Escape($StartMarker) + '(?s).*?' + [regex]::Escape($EndMarker)
    if ($Content -notmatch $pattern) { throw "Marker pair not found: $StartMarker / $EndMarker" }
    $body = if ($Lines.Count -gt 0) { ($Lines -join "`r`n") + "`r`n`t" } else { '' }
    $replacement = "$StartMarker`r`n`t$body$EndMarker"
    return [regex]::Replace($Content, $pattern, { param($m) $replacement }, 1)
}

$beforeLines = @($uiHooks | Where-Object { $_.Phase -eq 'Before' } | ForEach-Object { "$($_.Function)() // $($_.Mod)" })
$afterLines = @($uiHooks | Where-Object { $_.Phase -eq 'After' } | ForEach-Object { "$($_.Function)() // $($_.Mod)" })
$menusContent = Set-MarkerBlock $menusContent '// === PS4_MOD_HOOKS_BEFORE_START ===' '// === PS4_MOD_HOOKS_BEFORE_END ===' $beforeLines
$menusContent = Set-MarkerBlock $menusContent '// === PS4_MOD_HOOKS_AFTER_START ===' '// === PS4_MOD_HOOKS_AFTER_END ===' $afterLines
if ($PSCmdlet.ShouldProcess($menusPath, "Regenerate PS4 mod hook dispatch ($($beforeLines.Count) Before, $($afterLines.Count) After)")) {
    [System.IO.File]::WriteAllText($menusPath, $menusContent, $utf8NoBom)
}

[pscustomobject]@{
    Mods = $report
    ScriptsRsonBlocks = $allBlocks.Count
    ScriptsRsonDestination = $rsonDestination
    VpkTargetsTouched = ($copiedTargets -join ', ')
    Hooks = $hooks
}
