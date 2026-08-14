[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $Manifest = (Join-Path $PSScriptRoot '..\config\stage1-manifest.json'),
    [switch] $Clean
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configPath = [System.IO.Path]::GetFullPath($Config)
$manifestPath = [System.IO.Path]::GetFullPath($Manifest)
& (Join-Path $PSScriptRoot 'Test-Environment.ps1') -Config $configPath | Out-Null
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Stage manifest not found: $manifestPath" }
. (Join-Path $PSScriptRoot 'Resolve-ModSource.ps1')
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$stageManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$workRoot = if ([System.IO.Path]::IsPathRooted($settings.workRoot)) { [System.IO.Path]::GetFullPath($settings.workRoot) } else { [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $settings.workRoot)) }
$stageRoot = Join-Path $workRoot 'stage1\loose'
if ($Clean -and (Test-Path -LiteralPath $stageRoot) -and $PSCmdlet.ShouldProcess($stageRoot, 'Remove existing generated staging directory')) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
foreach ($mod in $stageManifest.mods) {
    $source = Get-ModSourceDir -Settings $settings -Mod $mod -RepositoryRoot $repositoryRoot
    $destination = Join-Path $stageRoot $mod.destinationSubdirectory
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Manifest source for '$($mod.name)' was not found: $source" }
    if ($PSCmdlet.ShouldProcess($destination, "Stage loose content from $source")) {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
    }
    [pscustomobject]@{ Mod = $mod.name; Source = $source; Destination = $destination }
}