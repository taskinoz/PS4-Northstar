[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Output = (Join-Path $PSScriptRoot '..\dist\northstar-profile'),
    [switch] $SkipR2ModStage,
    [switch] $Clean
)
if ($Clean) { throw 'Automatic overlay cleanup is retired. Use a fresh profile output and a clean retail install.' }
Write-Warning 'New-Stage2R2Overlay is deprecated; use New-NorthstarProfile. Output now contains R2Northstar/mods; nothing is flattened into r2.'
& (Join-Path $PSScriptRoot 'New-NorthstarProfile.ps1') -Config $Config -Output $Output -WhatIf:$WhatIfPreference
