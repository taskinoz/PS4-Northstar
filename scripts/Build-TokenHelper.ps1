<#
.SYNOPSIS
Builds the token helper app (token-helper/, Tauri) for this platform.

.DESCRIPTION
Installs the Tauri CLI with Bun (`bun install`), runs the Rust tests
(`cargo test`) unless -SkipTests, and builds the app with `bun tauri build`.
On Windows that gives NorthstarPS4TokenHelper.exe, copied to -Output; with
-Bundle, the NSIS installer too (Tauri downloads NSIS the first time).

Tauri builds for the system it runs on, so the macOS and Linux apps are built
by .github/workflows/token-helper.yml on GitHub's runners.

Needs Bun, Rust (cargo) and, on Windows, the Visual Studio C++ build tools.
#>
[CmdletBinding()]
param(
    [string] $Output = (Join-Path (Split-Path $PSScriptRoot) 'dist\token-helper'),
    [switch] $Bundle,
    [switch] $SkipTests
)
$ErrorActionPreference = 'Stop'
$source = Join-Path (Split-Path $PSScriptRoot) 'token-helper'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path

Push-Location $source
try {
    bun install --frozen-lockfile | Out-Host
    if ($LASTEXITCODE) { throw 'bun install failed' }
    if (-not $SkipTests) {
        Push-Location src-tauri
        try {
            cargo test | Out-Host
            if ($LASTEXITCODE) { throw 'cargo test failed' }
        } finally { Pop-Location }
    }
    $arguments = @('tauri', 'build')
    if ($Bundle) { $arguments += @('--bundles', 'nsis') } else { $arguments += '--no-bundle' }
    bun @arguments | Out-Host
    if ($LASTEXITCODE) { throw 'bun tauri build failed' }
} finally {
    Pop-Location
}

$release = Join-Path $source 'src-tauri\target\release'
$exe = if ($IsWindows -or $env:OS -eq 'Windows_NT') { 'NorthstarPS4TokenHelper.exe' } else { 'NorthstarPS4TokenHelper' }
Copy-Item -LiteralPath (Join-Path $release $exe) -Destination $Output -Force
if ($Bundle) {
    Get-ChildItem (Join-Path $release 'bundle\nsis') -Filter *.exe | Copy-Item -Destination $Output -Force
}
Get-ChildItem $Output -File | Where-Object Name -like 'NorthstarPS4*' |
    ForEach-Object { '{0,-48} {1,12:N0} bytes' -f $_.Name, $_.Length }
