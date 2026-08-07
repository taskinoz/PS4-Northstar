[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Root = (Join-Path $PSScriptRoot '..\work\stage1\sparse')
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = [System.IO.Path]::GetFullPath($Root)
& (Join-Path $PSScriptRoot 'Resolve-Stage1Defines.ps1') -Root $resolvedRoot | Out-Host

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$patches = @(
    @{
        Path = 'frontend\scripts\vscripts\burnmeter\sh_boost_store.gnut'
        Pattern = '(?ms)\r?\n\t// custom stuff for arena\r?\n\tstring boostStoreMode = GetCurrentPlaylistVarString\( "boost_store_mode", "off" \)\r?\n\tif \( boostStoreMode == "arena" \)\r?\n\t\{.*?\r?\n\t\}\r?\n'
        Replacement = "`n"
    },
    @{
        Path = 'frontend\scripts\vscripts\burnmeter\sh_burnmeter.gnut'
        Pattern = '(?ms)\r?\n\t// more hacks for arena\r?\n\tif \( !\( ref in burn\.burnRewards \) && GetCurrentPlaylistVarString\( "boost_store_mode", "off" \) == "arena" \)\r?\n\t\treturn GetArenaLoadoutItemAsBurnReward\( ref \)\r?\n'
        Replacement = "`n"
    },
    @{
        Path = 'frontend\scripts\vscripts\ui\menu_private_match.nut'
        Pattern = '(?ms)\r?\n\t\t\telse\r?\n\t\t\t\{\r?\n\t\t\t\tbool shouldBreak = false\r?\n.*?\r?\n\t\t\t\}\r?\n(?=\t\t\})'
        Replacement = "`n"
    }
)

$changedFiles = 0
foreach ($patch in $patches) {
    $path = Join-Path $resolvedRoot $patch.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Stage 1 compatibility target not found: $path" }
    $source = [System.IO.File]::ReadAllText($path)
    $updated = [regex]::Replace($source, $patch.Pattern, $patch.Replacement)
    if ($updated -cne $source -and $PSCmdlet.ShouldProcess($path, 'Remove Northstar loader-dependent arena UI block')) {
        [System.IO.File]::WriteAllText($path, $updated, $utf8NoBom)
        $changedFiles++
    }
}

$menuLobbyPath = Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui\menu_lobby.nut'
if (-not (Test-Path -LiteralPath $menuLobbyPath -PathType Leaf)) { throw "Stage 1 compatibility target not found: $menuLobbyPath" }
$menuLobby = [System.IO.File]::ReadAllText($menuLobbyPath)
$progressionDialog = "void function ShowToggleProgressionDialog( var button )`n{`n`tDialogData dialogData`n`tdialogData.menu = GetMenu( `"AnnouncementDialog`" )`n`tdialogData.header = `"#PROGRESSION_TOGGLE_ENABLED_HEADER`"`n`tdialogData.message = `"#PROGRESSION_TOGGLE_VANILLA`"`n`tdialogData.image = `$`"ui/menu/common/dialog_announcement_1`"`n`tOpenDialog( dialogData )`n}`n`n"
$menuLobbyUpdated = [regex]::Replace(
    $menuLobby,
    '(?ms)void function ShowToggleProgressionDialog\( var button \)\r?\n\{.*?(?=void function EnableProgression\(\))',
    $progressionDialog
)
$menuLobbyUpdated = [regex]::Replace($menuLobbyUpdated, '(?m)^\tProgression_SetPreference\( (?:true|false) \)\r?\n', '')
$menuLobbyUpdated = [regex]::Replace($menuLobbyUpdated, '(?m)^\tUpdateCachedLoadouts_Delayed\(\)\r?\n', '')
if ($menuLobbyUpdated -cne $menuLobby -and $PSCmdlet.ShouldProcess($menuLobbyPath, 'Disable loader-managed progression UI calls')) {
    [System.IO.File]::WriteAllText($menuLobbyPath, $menuLobbyUpdated, $utf8NoBom)
    $changedFiles++
}

$panelMainMenuPath = Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui\panel_mainmenu.nut'
if (-not (Test-Path -LiteralPath $panelMainMenuPath -PathType Leaf)) { throw "Stage 1 compatibility target not found: $panelMainMenuPath" }
$panelMainMenu = [System.IO.File]::ReadAllText($panelMainMenuPath)
$panelMainMenuUpdated = [regex]::Replace(
    $panelMainMenu,
    '(?ms)\r?\n\t// MOD SETTINGS\r?\n\tvar modSettingsButton = AddComboButton\( comboStruct, headerIndex, buttonIndex\+\+, "#MOD_SETTINGS" \)\r?\n\tHud_AddEventHandler\( modSettingsButton, UIE_CLICK, AdvanceMenuEventHandler\( GetMenu\( "ModSettings" \) \) \)\r?\n',
    "`n"
)
$panelMainMenuUpdated = [regex]::Replace($panelMainMenuUpdated, '(?m)^\tthread UpdateCustomMainMenuPromos\(\)\r?\n', '')
$panelMainMenuUpdated = [regex]::Replace(
    $panelMainMenuUpdated,
    '(?ms)void function TryUnlockNorthstarButton\(\)\r?\n\{.*?(?=void function OnPlayFDButton_Activate)',
    "void function TryUnlockNorthstarButton()`n{`n`tHud_SetLocked( file.fdButton, false )`n}`n`n"
)
$panelMainMenuUpdated = [regex]::Replace(
    $panelMainMenuUpdated,
    '(?ms)void function OnPlayNSButton_Activate\( var button \)\r?\n\{.*?(?=void function CancelNSLocalAuth\(\))',
    "void function OnPlayNSButton_Activate( var button )`n{`n`tif ( !Hud_IsLocked( button ) )`n`t`tAdvanceMenu( GetMenu( `"DirectConnectMenu`" ) )`n}`n`n"
)
$panelMainMenuUpdated = [regex]::Replace(
    $panelMainMenuUpdated,
    '(?ms)void function UpdateCustomMainMenuPromos\(\)\r?\n\{.*?(?=void function SetSpotlightButtonData)',
    "void function UpdateCustomMainMenuPromos()`n{`n}`n`nvoid function UpdateCustomMainMenuPromosThreaded()`n{`n}`n`nvoid function UpdateWhatsNewData()`n{`n`tRuiSetBool( file.whatsNew, `"isVisible`", false )`n}`n`nvoid function UpdateSpotlightData()`n{`n`tHud_SetVisible( file.spotlightPanel, false )`n}`n`n"
)
if ($panelMainMenuUpdated -cne $panelMainMenu -and $PSCmdlet.ShouldProcess($panelMainMenuPath, 'Replace native Northstar auth and promo calls with Stage 1 direct-connect behavior')) {
    [System.IO.File]::WriteAllText($panelMainMenuPath, $panelMainMenuUpdated, $utf8NoBom)
    $changedFiles++
}
$modSettingsCallerFiles = @(
    (Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui\menu_lobby.nut'),
    (Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui\menu_ingame.nut')
)
foreach ($modSettingsCallerPath in $modSettingsCallerFiles) {
    $modSettingsCaller = [System.IO.File]::ReadAllText($modSettingsCallerPath)
    $modSettingsCallerUpdated = [regex]::Replace(
        $modSettingsCaller,
        '(?m)^\s*// MOD SETTINGS\r?\n\s*var modSettingsButton = AddComboButton\([^\r\n]*\)\r?\n\s*Hud_AddEventHandler\( modSettingsButton, UIE_CLICK, AdvanceMenuEventHandler\( GetMenu\( "ModSettings" \) \) \)\r?\n',
        ''
    )
    if ($modSettingsCallerUpdated -cne $modSettingsCaller -and $PSCmdlet.ShouldProcess($modSettingsCallerPath, 'Remove loader-owned Mod Settings registration')) {
        [System.IO.File]::WriteAllText($modSettingsCallerPath, $modSettingsCallerUpdated, $utf8NoBom)
        $changedFiles++
    }
}
$mouseCaptureFiles = Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui') -File |
    Where-Object { $_.Extension -in '.nut', '.gnut' }
foreach ($mouseCaptureFile in $mouseCaptureFiles) {
    $mouseCaptureSource = [System.IO.File]::ReadAllText($mouseCaptureFile.FullName)
    $mouseCaptureUpdated = [regex]::Replace($mouseCaptureSource, '(?m)^\s*AddMouseMovementCaptureHandler\([^\r\n]*\)\r?\n', '')
    if ($mouseCaptureUpdated -cne $mouseCaptureSource -and $PSCmdlet.ShouldProcess($mouseCaptureFile.FullName, 'Remove PC mouse-capture registration from PS4 UI')) {
        [System.IO.File]::WriteAllText($mouseCaptureFile.FullName, $mouseCaptureUpdated, $utf8NoBom)
        $changedFiles++
    }
}
$forbidden = Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'frontend\scripts\vscripts') -Recurse -File |
    Where-Object { $_.Extension -in '.nut', '.gnut' -and $_.Name -ne 'ui_mouse_capture.nut' } |
    Select-String -Pattern 'PopulateArenaLoadouts|GetArenaLoadoutItemAsBurnReward|Progression_GetPreference|Progression_SetPreference|UpdateCachedLoadouts_Delayed|AddMouseMovementCaptureHandler|`t#if'
if ($forbidden) { throw "Stage 1 frontend still references loader-managed APIs: $($forbidden.Path -join ', ')" }

$modSettingsCallers = Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui') -File |
    Where-Object { $_.Name -ne 'menu_mod_settings.nut' } |
    Select-String -SimpleMatch 'GetMenu( "ModSettings" )'
if ($modSettingsCallers) { throw "Stage 1 UI still contains runtime ModSettings callers: $($modSettingsCallers.Path -join ', ')" }
$panelNativeForbidden = Select-String -LiteralPath $panelMainMenuPath -Pattern 'NSIsMasterServerAuthenticated|NSTryAuthWithLocalServer|NSIsAuthenticatingWithServer|NSWasAuthSuccessful|NSCompleteAuthWithLocalServer|NSGetAuthFailReason|NSRequestCustomMainMenuPromos|NSHasCustomMainMenuPromoData|NSGetCustomMainMenuPromoData'
if ($panelNativeForbidden) { throw "Stage 1 main menu still references Northstar native auth/promo APIs: $panelMainMenuPath" }
$privateMatchPath = Join-Path $resolvedRoot 'frontend\scripts\vscripts\ui\menu_private_match.nut'
$privateMatchForbidden = Select-String -LiteralPath $privateMatchPath -Pattern 'GetPrivateMatchSettingCategories|GetPrivateMatchCustomSettingsForCategory|CustomMatchSettingContainer'
if ($privateMatchForbidden) { throw "Stage 1 private-match menu still references CustomServers APIs: $privateMatchPath" }

[pscustomobject]@{ Root = $resolvedRoot; ChangedFiles = $changedFiles; LoaderManagedDependencies = 0 }