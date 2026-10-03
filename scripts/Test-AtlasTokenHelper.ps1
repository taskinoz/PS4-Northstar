<#
.SYNOPSIS
Tests Start-AtlasTokenHelper.ps1 against a fake EA app and a fake Atlas.

.DESCRIPTION
Nothing here touches a real account: tests/token_helper/fake_services.py plays
the EA app's LSX server and Atlas's /client/origin_auth on local ports, and the
helper writes its identity and pairing key into a temporary folder. Checks:
the LSX key schedule against origin-sdk's published vector, the handshake and
GetAuthCode, the identity file, the served endpoint with the right key, a
wrong key and an unknown path, and the minimum interval between tokens.
Needs Python with the `cryptography` package.
#>
[CmdletBinding()]
param([int] $LsxPort = 39216, [int] $AtlasPort = 39217, [int] $HelperPort = 39218)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$work = Join-Path ([IO.Path]::GetTempPath()) ("ns-token-helper-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$state = Join-Path $work 'events.jsonl'
$identity = Join-Path $work 'atlas_identity.json'
$keyFile = Join-Path $work 'token-helper.json'
$failures = 0
function Check([bool] $condition, [string] $what) {
    if ($condition) { Write-Host "ok   $what" } else { Write-Host "FAIL $what"; $script:failures++ }
}
function Events { if (Test-Path $state) { Get-Content $state | ForEach-Object { $_ | ConvertFrom-Json } } }

# Start-Process joins its arguments without quoting them, and the repository
# path may contain spaces.
function Quote([string] $text) { '"' + $text + '"' }
$fakes = Start-Process python -ArgumentList @((Quote (Join-Path $root 'tests\token_helper\fake_services.py')), $LsxPort, $AtlasPort, (Quote $state)) -PassThru -WindowStyle Hidden
$helper = $null
try {
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Events | Where-Object event -eq 'ready') -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    Check ([bool](Events | Where-Object event -eq 'ready')) 'fake services started (LSX key vector checked)'

    $common = @('-LsxPort', $LsxPort, '-MasterServer', "http://127.0.0.1:$AtlasPort", '-Output', $identity, '-KeyFile', $keyFile,
        '-Port', $HelperPort)
    $quoted = @('-LsxPort', $LsxPort, '-MasterServer', "http://127.0.0.1:$AtlasPort", '-Output', (Quote $identity),
        '-KeyFile', (Quote $keyFile), '-Port', $HelperPort)
    $script = Join-Path $root 'scripts\Start-AtlasTokenHelper.ps1'

    # -Once: handshake, auth code, Atlas exchange, identity file.
    $out = & pwsh -NoProfile -File $script -Once @common 2>&1 | Out-String
    $events = @(Events)
    Check ([bool]($events | Where-Object { $_.event -eq 'handshake' -and $_.ok })) 'LSX challenge handshake accepted'
    Check ([bool]($events | Where-Object { $_.event -eq 'authcode' -and $_.ok })) 'GetAuthCode with the right user and client id'
    $issued = @($events | Where-Object event -eq 'token')
    Check ($issued.Count -eq 1) 'one Atlas token minted'
    $file = Get-Content $identity -Raw | ConvertFrom-Json
    $pair = Get-Content $keyFile -Raw | ConvertFrom-Json
    Check ($file.uid -eq '1012345678901' -and $file.playerToken -eq $issued[0].token) 'identity file holds the uid and the minted token'
    Check ($file.refreshUrl -eq "http://127.0.0.1:$HelperPort/atlas/token" -and $file.refreshKey -eq $pair.key) 'identity file names the helper and its key'
    Check ($out -notmatch $issued[0].token -and $out -notmatch $pair.key) 'neither the token nor the key is printed'

    # Serve mode.
    $helper = Start-Process pwsh -ArgumentList (@('-NoProfile', '-File', (Quote $script), '-MinSecondsBetweenTokens', '0') + $quoted) -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(20)
    while (@(Events | Where-Object event -eq 'token').Count -lt 2 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Seconds 1
    $url = "http://127.0.0.1:$HelperPort/atlas/token"
    $reply = Invoke-RestMethod -Uri $url -Headers @{ 'X-NorthstarPS4-Key' = $pair.key } -TimeoutSec 20
    $issued = @(Events | Where-Object event -eq 'token')
    Check ($reply.uid -eq '1012345678901' -and $reply.playerToken -eq $issued[-1].token) 'the endpoint returns a fresh token for the paired key'
    Check ((Get-Content $identity -Raw | ConvertFrom-Json).playerToken -eq $reply.playerToken) 'the identity file follows the served token'
    $status = 0
    try { Invoke-RestMethod -Uri $url -Headers @{ 'X-NorthstarPS4-Key' = ('0' * 32) } -TimeoutSec 10 | Out-Null } catch { $status = [int]$_.Exception.Response.StatusCode }
    Check ($status -eq 403) 'a wrong key is refused (403)'
    $status = 0
    try { Invoke-RestMethod -Uri "http://127.0.0.1:$HelperPort/other" -TimeoutSec 10 | Out-Null } catch { $status = [int]$_.Exception.Response.StatusCode }
    Check ($status -eq 404) 'an unknown path is refused (404)'
} finally {
    if ($helper -and -not $helper.HasExited) { Stop-Process -Id $helper.Id -Force }
    if ($fakes -and -not $fakes.HasExited) { Stop-Process -Id $fakes.Id -Force }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failures) { throw "$failures token helper check(s) failed" }
Write-Host 'Token helper tests passed.'
