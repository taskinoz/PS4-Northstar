[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot = Join-Path $repositoryRoot ('work\profile-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
# The keyvalues suite also takes the real playlist and its two shipped patches
# when they are on this machine, because the merge has to hold against 360 KB
# of genuine content and not just the unit fixtures. It skips them if absent.
$liveArgs = @()
$config = Join-Path $repositoryRoot 'config\local.json'
if (Test-Path -LiteralPath $config -PathType Leaf) {
    $settings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    $candidates = @(
        (Join-Path $repositoryRoot 'work\stage1\extracted\frontend\playlists_v2.txt'),
        (Join-Path $settings.northstarModsRoot 'Northstar.Custom\keyvalues\playlists_v2.txt'),
        (Join-Path $settings.northstarModsRoot 'Northstar.CustomServers\keyvalues\playlists_v2.txt'))
    if (($candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }).Count -eq 3) { $liveArgs = $candidates }
}
# The rpaks suite takes the shipped rpak.json when it is on this machine, so
# the Preload/Postload rules are checked against the real file and not only
# against fixtures.
$rpakArgs = @()
if ($settings) {
    $rpakConfig = Join-Path $settings.northstarModsRoot 'Northstar.Custom\paks\rpak.json'
    if (Test-Path -LiteralPath $rpakConfig -PathType Leaf) { $rpakArgs = @($rpakConfig) }
}
foreach ($suite in @('mod_catalog', 'json_text', 'keyvalues', 'rpaks')) {
    $exe = Join-Path $testRoot ($suite + '.exe')
    & clang++.exe -std=c++17 -D_CRT_SECURE_NO_WARNINGS -I (Join-Path $repositoryRoot 'launcher\include') (Join-Path $repositoryRoot ('tests\' + $suite + '.cpp')) -o $exe
    if ($LASTEXITCODE) { throw "Host compilation failed: $suite" }
    if ($suite -eq 'keyvalues') { & $exe @liveArgs } elseif ($suite -eq 'rpaks') { & $exe @rpakArgs } else { & $exe }
    if ($LASTEXITCODE) { throw "Host tests failed: $suite" }
    # Every other KeyValues file a shipped mod patches, merged for real. The
    # runtime refuses to serve a merge larger than the original because the
    # engine reads it short, so a file that stops fitting silently reverts to
    # vanilla - this turns that into a test failure instead.
    if ($suite -eq 'keyvalues' -and $liveArgs.Count -eq 3) {
        $vpkFor = [ordered]@{
            'resource/fontfiletable.txt'                  = 'frontend'
            'scripts/aisettings/npc_pilot_elite.txt'      = 'mp_common'
            'scripts/weapons/melee_pilot_arena.txt'       = 'mp_common'
            'scripts/weapons/melee_pilot_emptyhanded.txt' = 'mp_common'
            'scripts/weapons/melee_pilot_sword.txt'       = 'mp_common'
            'scripts/weapons/mp_weapon_softball.txt'      = 'mp_common'
            'scripts/weapons/mp_weapon_wingman.txt'       = 'mp_common'
            'scripts/weapons/mp_weapon_wingman_n.txt'     = 'mp_common'
        }
        # Lowest mod priority first, the order the runtime merges in.
        $mods = @('Northstar.Client', 'Northstar.CustomServers', 'Northstar.DirectConnect', 'Northstar.Custom')
        foreach ($relative in $vpkFor.Keys) {
            $native = $relative -replace '/', '\'
            $original = Join-Path $repositoryRoot ('work\stage1\extracted\' + $vpkFor[$relative] + '\' + $native)
            if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { continue }
            $patches = @()
            foreach ($mod in $mods) {
                $patch = Join-Path $settings.northstarModsRoot ($mod + '\keyvalues\' + $native)
                if (Test-Path -LiteralPath $patch -PathType Leaf) { $patches += $patch }
            }
            if ($patches.Count -eq 0) { continue }
            & $exe --fit $original @patches
            if ($LASTEXITCODE) { throw "KeyValues merge no longer fits: $relative" }
        }
    }
}
function Assert($Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
$source = Join-Path $testRoot 'pc\R2Northstar\mods'
$mod = Join-Path $source 'A.Mod'
New-Item -ItemType Directory -Path (Join-Path $mod 'mod\scripts'), (Join-Path $mod 'keyvalues') -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $mod 'mod.json'), '{"Name":"Display Name","Version":"1.0","LoadPriority":42}', $utf8)
[IO.File]::WriteAllText((Join-Path $mod 'mod\scripts\test.nut'), 'unmodified script', $utf8)
[IO.File]::WriteAllText((Join-Path $mod 'keyvalues\test.txt'), 'unmodified keyvalues', $utf8)
$enabled = Join-Path (Split-Path $source -Parent) 'enabledmods.json'
[IO.File]::WriteAllText($enabled, '{"Display Name":{"1.0":false},"Version":1}', $utf8)
$config = Join-Path $testRoot 'config.json'
[IO.File]::WriteAllText($config, (@{northstarModsRoot=$source} | ConvertTo-Json), $utf8)
$package = Join-Path $testRoot 'package'
$preview = Join-Path $testRoot 'preview'
& (Join-Path $PSScriptRoot 'New-NorthstarProfile.ps1') -Config $config -Output $preview -WhatIf | Out-Null
Assert (-not (Test-Path -LiteralPath $preview)) 'WhatIf wrote files.'
& (Join-Path $PSScriptRoot 'New-NorthstarProfile.ps1') -Config $config -Output $package | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
    $destination = Join-Path (Join-Path $package 'R2Northstar\mods') $file.FullName.Substring($source.Length + 1)
    Assert ((Get-FileHash -LiteralPath $file.FullName).Hash -eq (Get-FileHash -LiteralPath $destination).Hash) "File changed: $destination"
}
Assert ((Get-FileHash -LiteralPath $enabled).Hash -eq (Get-FileHash -LiteralPath (Join-Path $package 'R2Northstar\enabledmods.json')).Hash) 'Enabled settings changed.'
Assert (-not (Test-Path (Join-Path $package 'r2'))) 'Created flattened overlay.'
Assert (-not (Test-Path (Join-Path $package 'vpk_ps4'))) 'Created game archives.'
$rejected = $false
try { & (Join-Path $PSScriptRoot 'New-NorthstarProfile.ps1') -Config $config -Output $package | Out-Null } catch { $rejected = $true }
Assert $rejected 'Existing user profile was overwritten.'
foreach ($script in @('Build-AndDeployStage1Vpks.ps1','Build-Stage1ModIntegration.ps1','Apply-Stage1Compatibility.ps1','New-Stage1Workspace.ps1','Merge-Stage2ScriptsRson.ps1')) {
    $rejected = $false
    try { & (Join-Path $PSScriptRoot $script) | Out-Null } catch { $rejected = $_.Exception.Message -match 'retired' }
    Assert $rejected "Retired script did not stop: $script"
}
Write-Output "Profile tests passed. Fixtures: $testRoot"
