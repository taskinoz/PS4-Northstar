<#
.SYNOPSIS
Launches the game for a hands-on play session and preserves the log.

.DESCRIPTION
Invoke-Stage2Iteration.ps1 is for automated probes: it watches for a pattern
and kills the process. This script is for the opposite case - the user plays,
connects, and something crashes or hangs - so it never terminates the game and
it captures whatever the session produced.

The Qt launcher starts shadPS4 without --log-append, which truncates
shad_log.txt on every launch. A crash or hang investigated after the next
launch therefore has no evidence left. This script always passes --log-append
and archives the session's own lines under work/stage2/sessions/, so evidence
survives both a crash and a later relaunch.
#>
[CmdletBinding()]
param(
    [string]$Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string]$ShadPs4Exe = 'C:\Users\tristan\AppData\Roaming\shadPS4QtLauncher\versions\Pre-release\shadPS4.exe',
    [string]$ShadLog = 'C:\Users\tristan\AppData\Roaming\shadPS4\log\shad_log.txt',
    # Short note about what is being tested, e.g. 'fastball' or 'mp-lobby'.
    [string]$Label = 'session',
    # Safety net only; a play session normally ends when the window is closed.
    [ValidateRange(60, 36000)]
    [int]$MaxMinutes = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [IO.Path]::GetFullPath($Config)
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw "Configuration file not found: $configPath" }
$settings = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
$gameRoot = [IO.Path]::GetFullPath($settings.ps4GameRoot)
$eboot = Join-Path $gameRoot 'eboot.bin'
$prx = Join-Path $gameRoot 'bin\ps4_retail\northstar_ps4.prx'
foreach ($required in @($ShadPs4Exe, $eboot, $prx)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file not found: $required" }
}

$safeLabel = ($Label -replace '[^A-Za-z0-9._-]', '-')
$sessionRoot = Join-Path $repoRoot ('work\stage2\sessions\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $safeLabel)
[IO.Directory]::CreateDirectory($sessionRoot) | Out-Null
$transcriptPath = Join-Path $sessionRoot 'session.log'
$resultPath = Join-Path $sessionRoot 'result.json'

# Record which build actually ran. A session whose PRX hash does not match the
# build under test is the single most misleading thing in this workflow.
$installedHash = (Get-FileHash -LiteralPath $prx -Algorithm SHA256).Hash.ToLowerInvariant()
$buildInfoPath = Join-Path (Split-Path $prx -Parent) 'northstar_ps4.build.json'
$buildInfo = $null
if (Test-Path -LiteralPath $buildInfoPath -PathType Leaf) {
    $buildInfo = Get-Content -Raw -LiteralPath $buildInfoPath | ConvertFrom-Json
}

$logOffset = 0L
if (Test-Path -LiteralPath $ShadLog -PathType Leaf) { $logOffset = (Get-Item -LiteralPath $ShadLog).Length }

Write-Host "Installed PRX : $installedHash"
Write-Host "Session log   : $transcriptPath"
Write-Host 'Launching shadPS4 with --log-append. Play, then close the game window when done.'

$process = Start-Process -FilePath $ShadPs4Exe -ArgumentList @('--game', $eboot, '--log-append') -PassThru
$started = Get-Date
$deadline = $started.AddMinutes($MaxMinutes)
$timedOut = $false
try {
    while (-not $process.HasExited) {
        if ((Get-Date) -gt $deadline) { $timedOut = $true; break }
        Start-Sleep -Seconds 2
        $process.Refresh()
    }
} finally {
    # Copy whatever the session wrote, whether it exited cleanly, crashed, or
    # was closed after a hang.
    $captured = ''
    if (Test-Path -LiteralPath $ShadLog -PathType Leaf) {
        $stream = [IO.File]::Open($ShadLog, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            if ($stream.Length -lt $logOffset) { $logOffset = 0 }
            [void]$stream.Seek($logOffset, [IO.SeekOrigin]::Begin)
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 65536, $true)
            try { $captured = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    }
    [IO.File]::WriteAllText($transcriptPath, $captured, [Text.UTF8Encoding]::new($false))
}

$lines = if ($captured) { ($captured -split "`n").Count } else { 0 }
$markers = [ordered]@{
    UiLifecycleCompleted = [bool]($captured -match 'UI lifecycle completed')
    ClientVmCreated      = [bool]($captured -match 'VM initialized context=1')
    ClientLifecycleDone  = [bool]($captured -match 'CLIENT lifecycle completed')
    FatalError           = [bool]($captured -match 'FatalError')
    UnhandledException   = [bool]($captured -match 'Unhandled Exception')
}
$firstFatal = ([regex]::Match($captured, '.*(?:FatalError|Unhandled Exception).*')).Value

$result = [ordered]@{
    Label = $Label
    Started = $started.ToString('o')
    DurationMinutes = [Math]::Round(((Get-Date) - $started).TotalMinutes, 2)
    ProcessExited = $process.HasExited
    TimedOut = $timedOut
    Eboot = $eboot
    Prx = $prx
    PrxSha256 = $installedHash
    BuildInfo = $buildInfo
    CapturedLines = $lines
    Markers = $markers
    FirstFatalLine = $firstFatal
    Transcript = $transcriptPath
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding utf8
[pscustomobject]$result
