[CmdletBinding()]
param([string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'))

$ErrorActionPreference = 'Stop'
$configPath = [System.IO.Path]::GetFullPath($Config)
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Configuration not found: $configPath`nCopy config/project.example.json to config/local.json and adjust its paths."
}
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
foreach ($name in @('expectedPcBuild', 'expectedPs4Build', 'pcGameRoot', 'ps4GameRoot', 'northstarModsRoot', 'workRoot')) {
    if ([string]::IsNullOrWhiteSpace([string]$settings.$name)) { throw "Configuration setting '$name' is required." }
}
$requiredPaths = [ordered]@{ pcGameRoot = $settings.pcGameRoot; ps4GameRoot = $settings.ps4GameRoot; northstarModsRoot = $settings.northstarModsRoot }
foreach ($entry in $requiredPaths.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Container)) { throw "Path '$($entry.Key)' does not exist: $($entry.Value)" }
}
$pcBuildFile = Join-Path $settings.pcGameRoot 'build.txt'
$ps4BuildFile = Join-Path $settings.ps4GameRoot 'build.txt'
foreach ($buildFile in @($pcBuildFile, $ps4BuildFile)) {
    if (-not (Test-Path -LiteralPath $buildFile -PathType Leaf)) { throw "Build identifier not found: $buildFile" }
}
$pcBuild = (Get-Content -LiteralPath $pcBuildFile -Raw).Trim()
$ps4Build = (Get-Content -LiteralPath $ps4BuildFile -Raw).Trim()
# PC and PS4 builds use unrelated build-tagging schemes (e.g. PC's
# "Titanfall2_v2_0_11_0" post-launch version tag vs. PS4's
# "R2PS4_r2dlc11_598_CL297590_..." internal CL-stamped build string), so they
# are validated against separate expected values, not against each other.
if ($pcBuild -ne $settings.expectedPcBuild) { throw "PC build '$pcBuild' does not match expected build '$($settings.expectedPcBuild)'." }
if ($ps4Build -ne $settings.expectedPs4Build) { throw "PS4 build '$ps4Build' does not match expected build '$($settings.expectedPs4Build)'." }
$ps4VpkRoot = Join-Path $settings.ps4GameRoot 'vpk_ps4'
if (-not (Test-Path -LiteralPath $ps4VpkRoot -PathType Container)) { throw "PS4 VPK directory not found: $ps4VpkRoot" }
[pscustomobject]@{
    ExpectedPcBuild = $settings.expectedPcBuild; ExpectedPs4Build = $settings.expectedPs4Build
    PcBuild = $pcBuild; Ps4Build = $ps4Build
    PcGameRoot = [System.IO.Path]::GetFullPath($settings.pcGameRoot)
    Ps4GameRoot = [System.IO.Path]::GetFullPath($settings.ps4GameRoot)
    Ps4VpkCount = @(Get-ChildItem -LiteralPath $ps4VpkRoot -Filter '*.vpk' -File).Count; Status = 'Ready'
}