[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string]$ShadPs4Exe = 'C:\Users\tristan\AppData\Roaming\shadPS4QtLauncher\versions\Pre-release\shadPS4.exe',
    [string]$ShadLog = 'C:\Users\tristan\AppData\Roaming\shadPS4\log\shad_log.txt',
    [string]$SuccessPattern = '\[NorthstarPS4\] module tracker complete engine=1 client=1',
    [string]$FailurePattern = 'SIGSEGV|access violation|guest crash|Unhandled exception|\[Debug\] <Critical>',
    [ValidateRange(5, 1800)]
    [int]$TimeoutSeconds = 120,
    [switch]$SkipBuild,
    [switch]$SkipDeploy,
    [switch]$KeepRunning,
    [switch]$NoLaunch,
    [switch]$EnableDiagnosticConVar,
    [switch]$EnableTeamChangesConVar,
    [switch]$EnableDiagnosticUiNative,
    [switch]$EnableM6FsOverlay,
    [switch]$EnableM6ModMetadata,
    [switch]$EnableM6Scripts,
    [switch]$EnableM6ScriptProbe,
    [switch]$EnableM6ScriptInject,
    [switch]$EnableM6ScriptInjectFromMods,
    [switch]$EnableM6Localise,
    [switch]$SkipR2ModStage
)

$ErrorActionPreference = 'Stop'
if ($EnableM6Scripts) { throw 'Use New-NorthstarProfile.ps1 to prepare R2Northstar/mods; runtime iterations never stage or merge scripts.' }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [IO.Path]::GetFullPath($Config)
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Configuration file not found: $configPath"
}
$settings = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
$gameRoot = [IO.Path]::GetFullPath($settings.ps4GameRoot)
$eboot = Join-Path $gameRoot 'eboot.bin'
$prx = Join-Path $gameRoot 'bin\ps4_retail\northstar_ps4.prx'

foreach ($required in @($ShadPs4Exe, $eboot)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build-Stage2Poc.ps1') -EnableDiagnosticConVar:$EnableDiagnosticConVar -EnableTeamChangesConVar:$EnableTeamChangesConVar -EnableDiagnosticUiNative:$EnableDiagnosticUiNative -EnableM6FsOverlay:$EnableM6FsOverlay -EnableM6ModMetadata:$EnableM6ModMetadata -EnableM6ScriptProbe:$EnableM6ScriptProbe -EnableM6ScriptInject:$EnableM6ScriptInject -EnableM6ScriptInjectFromMods:$EnableM6ScriptInjectFromMods -EnableM6Localise:$EnableM6Localise
    if (-not $?) { throw 'Stage 2 build failed.' }
}
if (-not $SkipDeploy) {
    & (Join-Path $PSScriptRoot 'Deploy-Stage2Poc.ps1') -Config $configPath
    if (-not $?) { throw 'Stage 2 deployment failed.' }
}
if (-not (Test-Path -LiteralPath $prx -PathType Leaf)) {
    throw "Installed Stage 2 PRX not found: $prx"
}

$installedHash = (Get-FileHash -LiteralPath $prx -Algorithm SHA256).Hash.ToLowerInvariant()
if ($NoLaunch) {
    [pscustomobject]@{
        Ready = $true
        Eboot = $eboot
        Prx = $prx
        PrxSha256 = $installedHash
        ShadPs4 = $ShadPs4Exe
    }
    return
}

$iterationRoot = Join-Path $repoRoot ('work\stage2\iterations\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($iterationRoot) | Out-Null
$transcriptPath = Join-Path $iterationRoot 'shad-new-lines.log'
$resultPath = Join-Path $iterationRoot 'result.json'

$logOffset = 0L
if (Test-Path -LiteralPath $ShadLog -PathType Leaf) {
    $logOffset = (Get-Item -LiteralPath $ShadLog).Length
}

$arguments = @('--game', $eboot, '--log-append')
Write-Host "Launching shadPS4: $ShadPs4Exe"
Write-Host "Watching: $ShadLog"
Write-Host "Success: $SuccessPattern"
$process = Start-Process -FilePath $ShadPs4Exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
$started = Get-Date
$status = 'timeout'
$matchedLine = $null
$captured = [Text.StringBuilder]::new()

try {
    while (((Get-Date) - $started).TotalSeconds -lt $TimeoutSeconds) {
        if (Test-Path -LiteralPath $ShadLog -PathType Leaf) {
            $stream = [IO.File]::Open(
                $ShadLog,
                [IO.FileMode]::Open,
                [IO.FileAccess]::Read,
                [IO.FileShare]::ReadWrite)
            try {
                if ($stream.Length -lt $logOffset) { $logOffset = 0 }
                [void]$stream.Seek($logOffset, [IO.SeekOrigin]::Begin)
                $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
                try {
                    $newText = $reader.ReadToEnd()
                    $logOffset = $stream.Position
                } finally {
                    $reader.Dispose()
                }
            } finally {
                $stream.Dispose()
            }

            if ($newText) {
                [void]$captured.Append($newText)
                foreach ($line in ($newText -split '\r?\n')) {
                    if (-not $line) { continue }
                    if ($line -match 'NorthstarPS4') { Write-Host $line }
                    if ($line -match $FailurePattern) {
                        $status = 'failure'
                        $matchedLine = $line
                        break
                    }
                    if ($line -match $SuccessPattern) {
                        $status = 'success'
                        $matchedLine = $line
                        break
                    }
                }
                if ($status -ne 'timeout') { break }
            }
        }

        if ($process.HasExited) {
            $status = 'process-exited'
            break
        }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
} finally {
    [IO.File]::WriteAllText($transcriptPath, $captured.ToString(), [Text.UTF8Encoding]::new($false))
    if (-not $KeepRunning -and -not $process.HasExited) {
        Stop-Process -Id $process.Id
        [void]$process.WaitForExit(5000)
    }
}

$result = [ordered]@{
    Status = $status
    MatchedLine = $matchedLine
    Started = $started.ToString('o')
    DurationSeconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)
    ProcessId = $process.Id
    ProcessExited = $process.HasExited
    KeptRunning = [bool]$KeepRunning
    Eboot = $eboot
    Prx = $prx
    PrxSha256 = $installedHash
    ShadPs4 = $ShadPs4Exe
    Transcript = $transcriptPath
}
$result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding utf8
$resultObject = [pscustomobject]$result
$resultObject

if ($status -eq 'failure') { throw "shadPS4 failure pattern matched: $matchedLine" }
if ($status -eq 'timeout') { throw "Timed out after $TimeoutSeconds seconds. Transcript: $transcriptPath" }
if ($status -eq 'process-exited') { throw "shadPS4 exited before a result matched. Transcript: $transcriptPath" }