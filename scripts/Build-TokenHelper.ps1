<#
.SYNOPSIS
Builds the token helper (token-helper/, Go) for Windows, macOS and Linux.

.DESCRIPTION
Cross-compiles with the Go toolchain in tools/go-portable (or `go` on PATH),
with cgo off, so every platform builds here and needs nothing installed to run:
- NorthstarPS4TokenHelper.exe          Windows x64
- NorthstarPS4TokenHelper-macOS        macOS, one universal binary (Apple Silicon + Intel)
- NorthstarPS4TokenHelper-linux        Linux x64
Go's linker gives the Apple Silicon build the ad-hoc signature macOS requires;
the two macOS builds are joined into a universal ("fat") binary here, as lipo
would. Runs the Go tests first unless -SkipTests.
#>
[CmdletBinding()]
param(
    [string] $Output = (Join-Path (Split-Path $PSScriptRoot) 'dist\token-helper'),
    [string] $Version = '1.0.0',
    [switch] $SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$source = Join-Path $repo 'token-helper'
$go = Join-Path $repo 'tools\go-portable\go\bin\go.exe'
if (-not (Test-Path $go)) { $go = (Get-Command go -ErrorAction Stop).Source }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path

Push-Location $source
$saved = @{ GOOS = $env:GOOS; GOARCH = $env:GOARCH; CGO_ENABLED = $env:CGO_ENABLED }
try {
    if (-not $SkipTests) {
        & $go test ./... | Out-Host
        if ($LASTEXITCODE) { throw 'Go tests failed' }
    }
    $env:CGO_ENABLED = '0'
    $ldflags = "-s -w -X main.version=$Version"
    $builds = @(
        @{ os = 'windows'; arch = 'amd64'; out = 'NorthstarPS4TokenHelper.exe' },
        @{ os = 'darwin'; arch = 'arm64'; out = 'macos-arm64.tmp' },
        @{ os = 'darwin'; arch = 'amd64'; out = 'macos-x64.tmp' },
        @{ os = 'linux'; arch = 'amd64'; out = 'NorthstarPS4TokenHelper-linux' })
    foreach ($build in $builds) {
        $env:GOOS = $build.os; $env:GOARCH = $build.arch
        & $go build -trimpath -ldflags $ldflags -o (Join-Path $Output $build.out) .
        if ($LASTEXITCODE) { throw "go build failed for $($build.os)/$($build.arch)" }
    }
} finally {
    foreach ($name in $saved.Keys) { Set-Item "env:$name" $saved[$name] }
    Pop-Location
}

# Universal macOS binary: a big-endian fat header, then each Mach-O slice
# aligned to 2^14 bytes, with the CPU type and subtype from its own header.
function Read-UInt32LE([byte[]] $data, [int] $at) { [BitConverter]::ToUInt32($data, $at) }
function Write-UInt32BE($stream, [uint32] $value) {
    $bytes = [BitConverter]::GetBytes($value); [Array]::Reverse($bytes); $stream.Write($bytes, 0, 4)
}
$slices = foreach ($name in 'macos-arm64.tmp', 'macos-x64.tmp') {
    $data = [IO.File]::ReadAllBytes((Join-Path $Output $name))
    # MH_MAGIC_64, 0xfeedfacf (PowerShell reads hex literals that large as negative)
    if ((Read-UInt32LE $data 0) -ne 4277009103) { throw "$name is not a 64-bit Mach-O file" }
    , @($data, (Read-UInt32LE $data 4), (Read-UInt32LE $data 8))
}
$align = 14
$fat = New-Object IO.MemoryStream
Write-UInt32BE $fat 3405691582  # FAT_MAGIC, 0xcafebabe
Write-UInt32BE $fat ([uint32]$slices.Count)
$offset = [uint32](1 -shl $align)
$offsets = @()
foreach ($slice in $slices) {
    Write-UInt32BE $fat $slice[1]; Write-UInt32BE $fat $slice[2]
    Write-UInt32BE $fat $offset; Write-UInt32BE $fat ([uint32]$slice[0].Length); Write-UInt32BE $fat ([uint32]$align)
    $offsets += $offset
    $offset = [uint32]([Math]::Ceiling(($offset + $slice[0].Length) / (1 -shl $align)) * (1 -shl $align))
}
for ($i = 0; $i -lt $slices.Count; $i++) {
    $fat.SetLength($offsets[$i])
    $fat.Position = $offsets[$i]
    $fat.Write($slices[$i][0], 0, $slices[$i][0].Length)
}
[IO.File]::WriteAllBytes((Join-Path $Output 'NorthstarPS4TokenHelper-macOS'), $fat.ToArray())
Remove-Item -LiteralPath (Join-Path $Output 'macos-arm64.tmp'), (Join-Path $Output 'macos-x64.tmp')

Get-ChildItem $Output -File | Where-Object Name -like 'NorthstarPS4TokenHelper*' |
    ForEach-Object { '{0,-36} {1,12:N0} bytes' -f $_.Name, $_.Length }
