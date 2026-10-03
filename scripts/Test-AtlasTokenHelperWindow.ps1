<#
.SYNOPSIS
Tests the token helper window (NorthstarPS4TokenHelper.exe) against a fake EA app, Atlas and console.

.DESCRIPTION
Builds the exes from token-helper/src (Build-TokenHelper.ps1), opens the window
against tests/token_helper/fake_services.py, and drives it: the EA account shown,
nothing found, a wrong code, the right code, and a token served to the paired
game. -EaDown checks the message when the EA app cannot be reached instead.
WinForms controls show up to UI Automation here as plain panes, so they are
found by name and driven with window messages. Screenshots go to -Shots.
Run it with Windows PowerShell on an interactive desktop; it needs Python with
the `cryptography` package.
#>
param([string] $Shots = (Join-Path ([IO.Path]::GetTempPath()) 'ns-token-helper-window'), [switch] $EaDown)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NsTestWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
New-Item -ItemType Directory -Force $Shots | Out-Null
$work = Join-Path ([IO.Path]::GetTempPath()) ("ns-token-helper-window-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
$state = Join-Path $work 'events.jsonl'
function Q([string] $t) { '"' + $t + '"' }
function Events { if (Test-Path $state) { Get-Content $state | ForEach-Object { $_ | ConvertFrom-Json } } }
$AE = [Windows.Automation.AutomationElement]
$TS = [Windows.Automation.TreeScope]

function Shot($window, [string] $name) {
    $h = [IntPtr]$window.Current.NativeWindowHandle
    $r = New-Object NsTestWindow+RECT; [void][NsTestWindow]::GetWindowRect($h, [ref]$r)
    $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][NsTestWindow]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    $bmp.Save((Join-Path $Shots "$name.png")); $bmp.Dispose()
}
function Panes($window) { @($window.FindAll($TS::Descendants, [Windows.Automation.Condition]::TrueCondition)) }
function Pane($window, [string] $name) { Panes $window | Where-Object { $_.Current.Name -eq $name } | Select-Object -First 1 }
function Click($element) { [void][NsTestWindow]::SendMessage([IntPtr]$element.Current.NativeWindowHandle, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) }  # BM_CLICK
function SetText($element, [string] $text) { [void][NsTestWindow]::SendMessage([IntPtr]$element.Current.NativeWindowHandle, 0x000C, [IntPtr]::Zero, $text) }  # WM_SETTEXT
function AfterLabel($window, [string] $label) {
    $all = Panes $window
    for ($i = 0; $i -lt $all.Count - 1; $i++) { if ($all[$i].Current.Name -eq $label) { return $all[$i + 1] } }
}
function Texts($window) { (Panes $window | ForEach-Object { $_.Current.Name }) -join ' | ' }
function WaitText($window, [string] $pattern, [int] $seconds = 20) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) { if ((Texts $window) -match $pattern) { return $true }; Start-Sleep -Milliseconds 250 }
    return $false
}
$failed = 0
function Check([bool] $ok, [string] $what) { if ($ok) { Write-Host "ok   $what" } else { Write-Host "FAIL $what"; $script:failed++ } }

# The fake console listens on 127.0.0.1:39219 and 127.0.0.2:39221; the window
# looks for a game on 127.0.0.1:39221, finds none, and is pointed at 127.0.0.2.
$gui = $null
& (Join-Path $PSScriptRoot 'Build-TokenHelper.ps1') -Output (Join-Path $work 'bin') | Out-Null
$exe = Join-Path (Join-Path $work 'bin') 'NorthstarPS4TokenHelper.exe'
$fakes = Start-Process python -ArgumentList @((Q "$root\tests\token_helper\fake_services.py"), 39216, 39217, (Q $state), 39219) -PassThru -WindowStyle Hidden
try {
    Start-Sleep 2
    $lsx = if ($EaDown) { 39299 } else { 39216 }
    $gui = Start-Process $exe -PassThru -ArgumentList @('--lsx-port', $lsx, '--master-server', 'http://127.0.0.1:39217',
        '--output', (Q "$work\id.json"), '--key-file', (Q "$work\k.json"), '--port', 39218, '--console-port', 39221,
        '--min-seconds-between-tokens', 0)
    $window = $null
    $deadline = (Get-Date).AddSeconds(30)
    $cond = New-Object Windows.Automation.PropertyCondition($AE::NameProperty, 'NorthstarPS4 Token Helper')
    while (-not $window -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        $window = $AE::RootElement.FindFirst($TS::Children, $cond)
    }
    Check ([bool]$window) 'window opened'
    if (-not $window) { throw 'the window did not open' }
    if ($EaDown) {
        Check (WaitText $window 'Could not reach the EA app') 'EA app down: the reason is shown'
        Start-Sleep 1; Shot $window 'ea-down'
    } else {
        Check (WaitText $window 'Signed in to EA as account 1012345678901') 'EA account shown'
        Check (WaitText $window 'Choose where Northstar is running') 'asks where the game is (nothing found)'
        Start-Sleep 1; Shot $window '1-start'

        Click (Pane $window 'On a PS4, or in shadPS4 on another computer')
        $addressBox = AfterLabel $window 'Address'
        $codeBox = AfterLabel $window 'Code'
        SetText $addressBox '127.0.0.2'
        SetText $codeBox '1111'
        $button = Pane $window 'Sign in'
        Click $button
        Check (WaitText $window 'Wrong code') 'a wrong code is shown as refused'
        Start-Sleep 1; Shot $window '2-wrong-code'

        SetText $codeBox '4821'
        Click $button
        Check (WaitText $window 'Signed in Northstar on 127\.0\.0\.2') 'the right code signs the console in'
        $signin = @(Events | Where-Object event -eq 'signin')[-1]
        Check ($signin.ok -and $signin.token -eq @(Events | Where-Object event -eq 'token')[-1].token) 'the console got the newest token'

        # The game asking for a new token, as the console would.
        $key = (Get-Content "$work\k.json" -Raw | ConvertFrom-Json).key
        $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:39218/atlas/token' -Headers @{ 'X-NorthstarPS4-Key' = $key } -TimeoutSec 20
        Check ($reply.uid -eq '1012345678901') 'serves a new token to the paired game'
        Start-Sleep 1
        Shot $window '3-signed-in'
    }
} finally {
    if ($gui -and -not $gui.HasExited) { Stop-Process -Id $gui.Id -Force }
    if ($fakes -and -not $fakes.HasExited) { Stop-Process -Id $fakes.Id -Force }
    Start-Sleep -Milliseconds 500
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failed) { throw "$failed token helper window check(s) failed" }
Write-Host "Token helper window tests passed. Screenshots: $Shots"
