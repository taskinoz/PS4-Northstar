<#
.SYNOPSIS
Creates a PS4-layout overlay for one Northstar mod's RPaks and STARPaks.

.DESCRIPTION
The source mod is never modified. Every RPak is converted in isolation first,
then the results are merged into a fresh output directory. Shared STARPaks are
accepted only when both conversions produce identical bytes; otherwise the
conversion stops instead of silently clobbering one archive's texture data.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $SourceModDirectory,
    [Parameter(Mandatory)] [string] $Output
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceMod = [IO.Path]::GetFullPath($SourceModDirectory)
$outputRoot = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $sourceMod -PathType Container)) { throw "Mod directory not found: $sourceMod" }
if (Test-Path -LiteralPath $outputRoot) { throw "Conversion output already exists: $outputRoot" }

$paks = @(Get-ChildItem -LiteralPath (Join-Path $sourceMod 'paks') -Filter '*.rpak' -File -ErrorAction SilentlyContinue | Sort-Object Name)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if ($paks.Count -eq 0) {
    [pscustomobject]@{ Mod = (Split-Path $sourceMod -Leaf); Rpaks = 0; Files = 0 }
    return
}

$toolRoot = Join-Path $repositoryRoot 'work\rpak-converter'
$tool = Join-Path $toolRoot 'rpak_ps4_converter.exe'
$toolSource = Join-Path $repositoryRoot 'launcher\tools\rpak_ps4_converter.cpp'
$toolHeader = Join-Path $repositoryRoot 'launcher\include\northstar_ps4\rpak_texture_converter.h'
$needsBuild = -not (Test-Path -LiteralPath $tool -PathType Leaf)
if (-not $needsBuild) {
    $built = (Get-Item -LiteralPath $tool).LastWriteTimeUtc
    $needsBuild = (Get-Item -LiteralPath $toolSource).LastWriteTimeUtc -gt $built -or
        (Get-Item -LiteralPath $toolHeader).LastWriteTimeUtc -gt $built
}
if ($needsBuild) {
    New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
    & clang++.exe -std=c++17 -D_CRT_SECURE_NO_WARNINGS -I (Join-Path $repositoryRoot 'launcher\include') $toolSource -o $tool
    if ($LASTEXITCODE) { throw 'Failed to compile the PS4 RPak texture converter.' }
}

$scratch = Join-Path $toolRoot ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
try {
    $merged = Join-Path $scratch 'merged'
    New-Item -ItemType Directory -Path $merged -Force | Out-Null
    $skipped = @()
    foreach ($pak in $paks) {
        $one = Join-Path $scratch ([IO.Path]::GetFileNameWithoutExtension($pak.Name))
        & $tool $pak.FullName $one
        if ($LASTEXITCODE) {
            # One pak the converter cannot handle (the reason is printed above)
            # must not stop every other mod from syncing. Its PC-layout copy
            # stays in the profile, and the PS4 runtime refuses to load it.
            Write-Warning "Skipped $($pak.Name) in $(Split-Path $sourceMod -Leaf): it cannot be converted, and the PS4 runtime will not load it."
            $skipped += $pak.Name
            continue
        }
        foreach ($file in Get-ChildItem -LiteralPath $one -File) {
            $destination = Join-Path $merged $file.Name
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne
                    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
                    throw "Multiple RPaks require incompatible conversions of shared stream file: $($file.Name)"
                }
                continue
            }
            Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $merged -File) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $outputRoot $file.Name)
    }
    [pscustomobject]@{ Mod = (Split-Path $sourceMod -Leaf); Rpaks = $paks.Count; Files = @(Get-ChildItem -LiteralPath $outputRoot -File).Count; Skipped = $skipped.Count }
} finally {
    if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
