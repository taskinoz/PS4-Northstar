<#
.SYNOPSIS
Boots the installed runtime in shadPS4 repeatedly, signed in against a test EA
app and Atlas, and reports how many boots reach the Northstar lobby.

.DESCRIPTION
tests/token_helper/fake_services.py plays the EA app and Atlas on local ports;
the token helper (dist\token-helper\NorthstarPS4TokenHelper.exe, from
Build-TokenHelper.ps1) signs the game in against them, and ns_startup_args.txt
points the game at the test Atlas. Each boot launches the game through the
AI.Harness `launch` action, so the profile needs AI.Harness enabled.

The real atlas_identity.json is moved aside unread for the test and put back
afterwards; the script reports whether its size and date are unchanged.

-HostMatch also hosts a private match on mp_glitch after the boots, plays it for
30 seconds, returns to the lobby, and counts script errors.

Boot stability is measured over several boots (8 or more), not one.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 50)] [int] $Count = 8,
    [switch] $HostMatch,
    [int] $LsxPort = 39216,
    [int] $AtlasPort = 39217
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Env.ps1"
Set-Location $RepoRoot
$data = Join-Path (Get-ShadPs4UserDir) 'data\northstar_ps4'
$real = Join-Path $data 'atlas_identity.json'
$aside = Join-Path $data 'atlas_identity.json.real-aside'
$startupArgs = Join-Path $data 'ns_startup_args.txt'
$helper = Join-Path $RepoRoot 'dist\token-helper\NorthstarPS4TokenHelper.exe'
$shadPs4 = Get-ShadPs4Exe
$log = Join-Path (Get-ShadPs4UserDir) 'log\shad_log.txt'
if (-not (Test-Path -LiteralPath $helper)) { throw "Token helper not built: $helper (run Build-TokenHelper.ps1)" }
if (Test-Path -LiteralPath $aside) { throw "$aside already exists; restore it by hand first" }
if (Test-Path -LiteralPath $startupArgs) { throw "$startupArgs exists; move it aside first" }

$work = Join-Path $env:TEMP ('ns-bootloop-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
$state = Join-Path $work 'events.jsonl'
function Events { if (Test-Path $state) { Get-Content $state | ForEach-Object { $_ | ConvertFrom-Json } } }
function Status { try { & "$PSScriptRoot\Send-AIHarnessCommand.ps1" -Action status -TimeoutSeconds 20 } catch { $null } }
function Send-Console([string] $Command) { & "$PSScriptRoot\Send-AIHarnessCommand.ps1" -Action console -Command $Command -TimeoutSeconds 60 *>&1 | Out-Null }
function Start-Game([int] $TimeoutSeconds = 240) {
    Get-Process shadPS4 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 3
    foreach ($name in 'request.json', 'claimed.json') {
        $file = Join-Path $data "ai_harness\$name"
        if (Test-Path $file) { Remove-Item -LiteralPath $file -Force }
    }
    & "$PSScriptRoot\Send-AIHarnessCommand.ps1" -Action launch -QueueOnly *>&1 | Out-Null
    & "$PSScriptRoot\Invoke-TestBoot.ps1" -SkipBuild -SkipDeploy -KeepRunning -ShadPs4Exe $shadPs4 `
        -SuccessPattern 'menu_LobbyMenu menu opened' -TimeoutSeconds $TimeoutSeconds *>&1 | Out-Null
}

$realInfo = $null
if (Test-Path $real) {
    $realInfo = Get-Item $real | Select-Object Length, LastWriteTimeUtc
    Move-Item -LiteralPath $real -Destination $aside
}
$fakes = $null
try {
    $fakes = Start-Process python -ArgumentList @("`"$RepoRoot\tests\token_helper\fake_services.py`"", $LsxPort, $AtlasPort, "`"$state`"") -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Events | Where-Object event -eq 'ready') -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    & $helper --cli --once --lsx-port $LsxPort --master-server "http://127.0.0.1:$AtlasPort" `
        --key-file (Join-Path $work 'token-helper.json') --output $real --min-seconds-between-tokens 0 | Out-Null
    if (-not (Test-Path $real)) { throw 'The token helper wrote no identity.' }
    Set-Content -LiteralPath $startupArgs -Value "+ns_masterserver_hostname `"http://127.0.0.1:$AtlasPort`"" -Encoding ascii

    $reached = 0
    for ($i = 1; $i -le $Count; $i++) {
        $started = Get-Date
        $outcome = 'lobby'
        try { Start-Game } catch { $outcome = if ("$_" -match 'Timed out') { 'hang' } elseif ("$_" -match 'failure pattern') { 'crash' } else { 'error' } }
        if ($outcome -eq 'lobby') { $reached++ }
        "boot $i : $outcome ($([int]((Get-Date) - $started).TotalSeconds) s)"
    }
    "reached the lobby: $reached of $Count"

    if ($HostMatch) {
        $mark = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
        Start-Game -TimeoutSeconds 300
        "lobby: $((Status).level)"
        Send-Console 'setplaylist private_match'
        Send-Console 'map mp_glitch'
        $end = (Get-Date).AddSeconds(300)
        do { Start-Sleep 10; $s = Status } until (($s -and $s.level -eq 'mp_glitch') -or (Get-Date) -gt $end)
        "match: $($s.level)"
        Start-Sleep 30
        "after 30 s: $((Status).level), game running: $([bool](Get-Process shadPS4 -ErrorAction SilentlyContinue))"
        Send-Console 'map mp_lobby'
        $end = (Get-Date).AddSeconds(240)
        do { Start-Sleep 10; $s = Status } until (($s -and $s.level -eq 'mp_lobby') -or (Get-Date) -gt $end)
        "back: $($s.level)"
        $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        $stream.Position = [Math]::Min($mark, $stream.Length)
        $lines = (New-Object IO.StreamReader($stream)).ReadToEnd() -split "`n"
        $stream.Close()
        "script errors: $(@($lines | Where-Object { $_ -match 'SCRIPT ERROR' }).Count), fatal: $(@($lines | Where-Object { $_ -match 'FatalError|Unhandled Exception' }).Count)"
    }
    '--- test Atlas'
    Events | Where-Object event -ne 'token' | Group-Object event, ok | ForEach-Object { "$($_.Name): $($_.Count)" }
} finally {
    Get-Process shadPS4 -ErrorAction SilentlyContinue | Stop-Process -Force
    if ($fakes -and -not $fakes.HasExited) { Stop-Process -Id $fakes.Id -Force }
    Start-Sleep 1
    if (Test-Path $startupArgs) { Remove-Item -LiteralPath $startupArgs -Force }
    foreach ($file in @($real, "$real.tmp")) { if (Test-Path $file) { Remove-Item -LiteralPath $file -Force } }
    if (Test-Path $aside) { Move-Item -LiteralPath $aside -Destination $real }
    if ($realInfo) {
        $now = Get-Item $real | Select-Object Length, LastWriteTimeUtc
        "real identity restored unchanged: $($now.Length -eq $realInfo.Length -and $now.LastWriteTimeUtc -eq $realInfo.LastWriteTimeUtc)"
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
