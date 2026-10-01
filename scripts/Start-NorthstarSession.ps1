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

function Protect-SessionText([string]$Text) {
    if (-not $Text) { return '' }
    # Atlas credentials occur both in query strings and JSON. Preserve field
    # names so the request remains diagnosable without preserving its secret.
    $Text = [regex]::Replace($Text, '(?i)([?&](?:playerToken|authToken|token|password)=)[^&\s"'']+', '$1<redacted>')
    $Text = [regex]::Replace($Text, '(?i)("(?:playerToken|authToken|token|password)"\s*:\s*")[^"]*', '$1<redacted>')
    return $Text
}

function Write-RedactedLog([string]$Source, [string]$Destination) {
    $inputStream = [IO.File]::Open($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($inputStream, [Text.Encoding]::UTF8, $true, 65536, $true)
        $writer = [IO.StreamWriter]::new($Destination, $false, [Text.UTF8Encoding]::new($false), 65536)
        try {
            while (($line = $reader.ReadLine()) -ne $null) { $writer.WriteLine((Protect-SessionText $line)) }
        } finally {
            $writer.Dispose()
            $reader.Dispose()
        }
    } finally { $inputStream.Dispose() }
}

function Get-FileFingerprint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $Path
    [ordered]@{
        Path = $Path
        Bytes = $item.Length
        Sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

# Record which build actually ran. A session whose PRX hash does not match the
# build under test is the single most misleading thing in this workflow.
$installedHash = (Get-FileHash -LiteralPath $prx -Algorithm SHA256).Hash.ToLowerInvariant()
$buildInfoPath = Join-Path (Split-Path $prx -Parent) 'northstar_ps4.build.json'
$buildInfo = $null
if (Test-Path -LiteralPath $buildInfoPath -PathType Leaf) {
    $buildInfo = Get-Content -Raw -LiteralPath $buildInfoPath | ConvertFrom-Json
}

$binaryFingerprints = [ordered]@{}
foreach ($binary in @('eboot.bin', 'bin\ps4_retail\client.prx', 'bin\ps4_retail\engine.prx',
        'bin\ps4_retail\server.prx', 'bin\ps4_retail\northstar_ps4.prx')) {
    $fingerprint = Get-FileFingerprint (Join-Path $gameRoot $binary)
    if ($fingerprint) { $binaryFingerprints[$binary] = $fingerprint }
}

$dataRoot = Join-Path $env:APPDATA 'shadPS4\data\northstar_ps4'
$enabledPath = Join-Path $dataRoot 'enabledmods.json'
$enabledSettings = $null
if (Test-Path -LiteralPath $enabledPath -PathType Leaf) {
    try { $enabledSettings = Get-Content -Raw -LiteralPath $enabledPath | ConvertFrom-Json }
    catch { $enabledSettings = [ordered]@{ ParseError = $_.Exception.Message } }
}
$mods = @()
$modsRoot = Join-Path $gameRoot 'R2Northstar\mods'
if (Test-Path -LiteralPath $modsRoot -PathType Container) {
    foreach ($metadata in Get-ChildItem -LiteralPath $modsRoot -Filter mod.json -File -Recurse | Sort-Object FullName) {
        try {
            $mod = Get-Content -Raw -LiteralPath $metadata.FullName | ConvertFrom-Json
            $enabled = $true
            if ($enabledSettings -and $mod.Name -and $mod.Version) {
                $nameEntry = $enabledSettings.PSObject.Properties[$mod.Name]
                if ($nameEntry) {
                    $versionEntry = $nameEntry.Value.PSObject.Properties[[string]$mod.Version]
                    if ($versionEntry) { $enabled = [bool]$versionEntry.Value }
                }
            }
            $mods += [ordered]@{
                Folder = Split-Path (Split-Path $metadata.FullName -Parent) -Leaf
                Name = [string]$mod.Name
                Version = [string]$mod.Version
                LoadPriority = if ($null -ne $mod.LoadPriority) { [int]$mod.LoadPriority } else { 0 }
                Enabled = $enabled
                MetadataSha256 = (Get-FileHash -LiteralPath $metadata.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        } catch {
            $mods += [ordered]@{ Path = $metadata.FullName; ParseError = $_.Exception.Message }
        }
    }
}
$runtimeSchema = Get-FileFingerprint (Join-Path $dataRoot 'persistent_player_data_version_929.pdef')
if (-not $runtimeSchema) {
    $runtimeSchema = Get-FileFingerprint (Join-Path $modsRoot 'Northstar.PS4\mod\cfg\server\persistent_player_data_version_929.pdef')
}

$logOffset = 0L
if (Test-Path -LiteralPath $ShadLog -PathType Leaf) { $logOffset = (Get-Item -LiteralPath $ShadLog).Length }

# Preserve the preceding run before shadPS4 gets another chance to truncate or
# rotate it. This archive is also redacted; raw logs remain only in shadPS4's
# own log directory.
if ($logOffset -gt 0) {
    Write-RedactedLog $ShadLog (Join-Path $sessionRoot 'previous-redacted.log')
}

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
    $captured = Protect-SessionText $captured
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
$emulatorRevision = ([regex]::Match($captured, '(?im)^.*Run:\s*Revision\s*[:=]?\s*([0-9a-f]{7,40}).*$')).Groups[1].Value
$firstVmErrors = [ordered]@{}
foreach ($context in @('UI', 'CLIENT', 'SERVER')) {
    $match = [regex]::Match($captured, "(?im)^.*(?:SCRIPT (?:COMPILE )?ERROR.*\[$context\]|\[$context\].*SCRIPT (?:COMPILE )?ERROR).*$")
    $firstVmErrors[$context] = if ($match.Success) { $match.Value.Trim() } else { $null }
}

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
    EmulatorRevision = $emulatorRevision
    BinaryFingerprints = $binaryFingerprints
    Mods = $mods
    EnabledSettingsSha256 = if (Test-Path -LiteralPath $enabledPath -PathType Leaf) {
        (Get-FileHash -LiteralPath $enabledPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { $null }
    PersistenceSchema = $runtimeSchema
    CapturedLines = $lines
    Markers = $markers
    FirstFatalLine = $firstFatal
    FirstVmErrors = $firstVmErrors
    Transcript = $transcriptPath
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding utf8
[pscustomobject]$result
