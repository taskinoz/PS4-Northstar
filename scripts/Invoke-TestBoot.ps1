# Builds and deploys the runtime (unless skipped), launches shadPS4 on the game,
# and follows shadPS4's log until a line matches -SuccessPattern or
# -FailurePattern, or -TimeoutSeconds pass. The new log lines and a result.json
# go to work\test-boots\<time>. Throws unless the success pattern matched.
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ShadPs4Exe,
    [string]$ShadLog,
    [string]$SuccessPattern = '\[NorthstarPS4\] module tracker complete engine=1 client=1',
    [string]$FailurePattern = 'SIGSEGV|access violation|guest crash|Unhandled exception|\[Debug\] <Critical>',
    [ValidateRange(5, 1800)]
    [int]$TimeoutSeconds = 120,
    [switch]$SkipBuild,
    [switch]$SkipDeploy,
    [switch]$KeepRunning,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Env.ps1"
if (-not $ShadPs4Exe) { $ShadPs4Exe = Get-ShadPs4Exe }
if (-not $ShadLog) { $ShadLog = Join-Path (Get-ShadPs4UserDir) 'log\shad_log.txt' }
$gameRoot = Get-GameRoot
$eboot = Join-Path $gameRoot 'eboot.bin'
$prx = Join-Path $gameRoot 'bin\ps4_retail\northstar_ps4.prx'

foreach ($required in @($ShadPs4Exe, $eboot)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build-Northstar.ps1')
    if (-not $?) { throw 'Build failed.' }
}
if (-not $SkipDeploy) {
    & (Join-Path $PSScriptRoot 'Deploy-Runtime.ps1') -GameRoot $gameRoot
    if (-not $?) { throw 'Deployment failed.' }
}
if (-not (Test-Path -LiteralPath $prx -PathType Leaf)) {
    throw "Installed runtime not found: $prx"
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

$iterationRoot = Join-Path $RepoRoot ('work\test-boots\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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
