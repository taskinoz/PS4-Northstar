[CmdletBinding()]
param(
    [string] $Toolchain = (Join-Path $PSScriptRoot '..\tools\openorbis-0.5.4\OpenOrbis\PS4Toolchain'),
    [string] $Output = (Join-Path $PSScriptRoot '..\dist\northstar-ps4'),
    [switch] $EnableRuntimeManifest,
    [switch] $EnableExperimentalScriptLoading
)
$ErrorActionPreference = 'Stop'
# The normal development build discovers mods and serves loose overrides.
# Script injection remains opt-in: the PS4 VM lifecycle ABI is not yet proven.
$options = @{
    Toolchain = $Toolchain
    Output = $Output
    EnableM6FsOverlay = $true
    EnableM6ModMetadata = $true
    EnableM6Localise = $true
    EnableRuntimeManifest = $EnableRuntimeManifest
}
if ($EnableExperimentalScriptLoading) {
    $options.EnableM6ScriptProbe = $true
    $options.EnableM6ScriptInject = $true
    $options.EnableM6ScriptInjectFromMods = $true
}
& (Join-Path $PSScriptRoot 'Build-Stage2Poc.ps1') @options
if (-not $?) { throw 'Northstar PS4 build failed.' }
