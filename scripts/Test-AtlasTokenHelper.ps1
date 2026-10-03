<#
.SYNOPSIS
Tests the token helper (token-helper/, Go) in the terminal against a fake EA app, Atlas and console.

.DESCRIPTION
Nothing here touches a real account: tests/token_helper/fake_services.py plays
the EA app's LSX server, Atlas's /client/origin_auth and a console's sign-in
listener on local ports, and the helper writes its identity and pairing key
into a temporary folder. The Windows build is made from token-helper/ first
(Build-TokenHelper.ps1). Checks:
- the LSX key schedule against origin-sdk's published vector, the handshake
  and GetAuthCode;
- the identity file when no game is running;
- signing a console in: a wrong code, the right code, typed at the prompt,
  again by the paired key, and finding a running game by itself;
- --help and a bad option;
- the served endpoint with the right key, a wrong key and an unknown path;
- that nothing secret is printed.
Needs Python with the `cryptography` package.
#>
[CmdletBinding()]
param([int] $LsxPort = 39216, [int] $AtlasPort = 39217, [int] $HelperPort = 39218, [int] $ConsolePort = 39219,
    [int] $NoConsolePort = 39220)  # ConsolePort + 2 is the fake console's second address
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
function LastSignIn { @(Events | Where-Object event -eq 'signin')[-1] }
function LastToken { @(Events | Where-Object event -eq 'token')[-1].token }

# Start-Process joins its arguments without quoting them, and the repository
# path may contain spaces.
function Quote([string] $text) { '"' + $text + '"' }
& (Join-Path $PSScriptRoot 'Build-TokenHelper.ps1') -Output (Join-Path $work 'bin') -SkipTests | Out-Null
$exe = Join-Path (Join-Path $work 'bin') 'NorthstarPS4TokenHelper.exe'
$fakes = Start-Process python -ArgumentList @((Quote (Join-Path $root 'tests\token_helper\fake_services.py')), $LsxPort, $AtlasPort,
    (Quote $state), $ConsolePort) -PassThru -WindowStyle Hidden
$helper = $null
$common = @('--lsx-port', $LsxPort, '--master-server', "http://127.0.0.1:$AtlasPort", '--output', $identity, '--key-file', $keyFile,
    '--port', $HelperPort, '--min-seconds-between-tokens', '0')
function Run([string[]] $extra, [string] $stdin = '') {
    $arguments = $common + $extra
    if ($stdin) { $output = $stdin | & $exe @arguments 2>&1 | Out-String } else { $output = & $exe @arguments 2>&1 | Out-String }
    return @{ output = $output; exit = $LASTEXITCODE }
}
$printed = ''
try {
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Events | Where-Object event -eq 'ready') -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    Check ([bool](Events | Where-Object event -eq 'ready')) 'fake services started (LSX key vector checked)'

    # No game running: the identity file.
    $run = Run @('--once', '--local', '--console-port', $NoConsolePort)
    $printed += $run.output
    $events = @(Events)
    Check ([bool]($events | Where-Object { $_.event -eq 'handshake' -and $_.ok })) 'LSX challenge handshake accepted'
    Check ([bool]($events | Where-Object { $_.event -eq 'authcode' -and $_.ok })) 'GetAuthCode with the right user and client id'
    $file = Get-Content $identity -Raw | ConvertFrom-Json
    $pair = Get-Content $keyFile -Raw | ConvertFrom-Json
    Check ($run.exit -eq 0 -and $file.uid -eq '1012345678901' -and $file.playerToken -eq (LastToken)) 'no game running: the identity file holds the uid and the minted token'
    Check ($file.refreshUrl -eq "http://127.0.0.1:$HelperPort/atlas/token" -and $file.refreshKey -eq $pair.key) 'the identity file names the helper and its key'

    # A console: wrong code, right code, prompt, paired key, found by itself.
    $run = Run @('--once', '--console', '127.0.0.1 1111', '--console-port', $ConsolePort)
    $printed += $run.output
    Check ($run.exit -eq 1 -and $run.output -match 'wrong code' -and -not (LastSignIn).ok) 'a wrong code is refused and reported'
    $run = Run @('--once', '--console', '127.0.0.1 4821', '--console-port', $ConsolePort)
    $printed += $run.output
    $signin = LastSignIn
    Check ($run.exit -eq 0 -and $signin.ok -and -not $signin.byKey -and $signin.token -eq (LastToken) -and $signin.uid -eq '1012345678901') 'the right code signs the console in with the minted token'
    Check ($signin.refreshUrl -eq "http://127.0.0.1:$HelperPort/atlas/token") 'the sign-in names this helper'
    $run = Run @('--once', '--console-port', $ConsolePort, '--console', '127.0.0.1')
    $printed += $run.output
    Check ($run.exit -eq 0 -and (LastSignIn).ok -and (LastSignIn).byKey) 'a paired console is signed in again without a code'
    $before = @(Events | Where-Object event -eq 'signin').Count
    $run = Run @('--once', '--cli', '--console-port', $ConsolePort)
    $printed += $run.output
    Check ($run.exit -eq 0 -and @(Events | Where-Object event -eq 'signin').Count -eq $before + 1 -and (LastSignIn).ok) 'a running game is found and signed in without asking'
    # A console the helper has to ask about (127.0.0.2, ConsolePort + 2), typed at the prompt.
    Remove-Item -LiteralPath $keyFile
    $run = Run @('--once', '--cli', '--console-port', ($ConsolePort + 2)) '127.0.0.2 4821'
    $printed += $run.output
    Check ($run.exit -eq 0 -and $run.output -match 'Type the address and code' -and (LastSignIn).ok -and -not (LastSignIn).byKey) 'the address and code typed at the prompt sign the console in'
    $run = Run @('--help')
    Check ($run.exit -eq 0 -and $run.output -match '--console') '--help prints the options'
    $run = Run @('--nonsense')
    Check ($run.exit -eq 2 -and $run.output -match 'Unknown option') 'a bad option is refused (exit 2)'

    # Serve mode.
    $pair = Get-Content $keyFile -Raw | ConvertFrom-Json
    $helper = Start-Process $exe -ArgumentList (@('--lsx-port', $LsxPort, '--master-server', "http://127.0.0.1:$AtlasPort",
        '--output', (Quote $identity), '--key-file', (Quote $keyFile), '--port', $HelperPort, '--min-seconds-between-tokens', '0',
        '--local', '--console-port', $NoConsolePort)) -PassThru -WindowStyle Hidden
    $url = "http://127.0.0.1:$HelperPort/atlas/token"
    $deadline = (Get-Date).AddSeconds(20)
    $reply = $null
    while (-not $reply -and (Get-Date) -lt $deadline) {
        try { $reply = Invoke-RestMethod -Uri $url -Headers @{ 'X-NorthstarPS4-Key' = $pair.key } -TimeoutSec 20 } catch { Start-Sleep -Milliseconds 300 }
    }
    Check ($reply.uid -eq '1012345678901' -and $reply.playerToken -eq (LastToken)) 'the endpoint returns a fresh token for the paired key'
    Check ((Get-Content $identity -Raw | ConvertFrom-Json).playerToken -eq $reply.playerToken) 'the identity file follows the served token'
    $status = 0
    try { Invoke-RestMethod -Uri $url -Headers @{ 'X-NorthstarPS4-Key' = ('0' * 32) } -TimeoutSec 10 | Out-Null } catch { $status = [int]$_.Exception.Response.StatusCode }
    Check ($status -eq 403) 'a wrong key is refused (403)'
    $status = 0
    try { Invoke-RestMethod -Uri "http://127.0.0.1:$HelperPort/other" -TimeoutSec 10 | Out-Null } catch { $status = [int]$_.Exception.Response.StatusCode }
    Check ($status -eq 404) 'an unknown path is refused (404)'

    $secrets = @(Events | Where-Object event -eq 'token' | ForEach-Object token) + @($pair.key)
    Check (-not ($secrets | Where-Object { $printed -match $_ })) 'neither a token nor the key is printed'
} finally {
    if ($helper -and -not $helper.HasExited) { Stop-Process -Id $helper.Id -Force }
    if ($fakes -and -not $fakes.HasExited) { Stop-Process -Id $fakes.Id -Force }
    Start-Sleep -Milliseconds 500
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failures) { throw "$failures token helper check(s) failed" }
Write-Host 'Token helper tests passed.'
exit 0
