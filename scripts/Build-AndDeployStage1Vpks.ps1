[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [string] $Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string] $SparseRoot = (Join-Path $PSScriptRoot '..\work\stage1\sparse'),
    [string] $IndexRoot = (Join-Path $PSScriptRoot '..\work\stage1\original-indexes'),
    [string] $OutputRoot = (Join-Path $PSScriptRoot '..\dist\stage1-sparse'),
    [string] $RspnVpk = (Join-Path $PSScriptRoot '..\tools\RSPNVPK-bin\RSPNVPK.exe'),
    [switch] $NoBackup
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-ProjectPath([string] $Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$configPath = Resolve-ProjectPath $Config
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw "Project configuration not found: $configPath" }

$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$sparsePath = Resolve-ProjectPath $SparseRoot
$indexPath = Resolve-ProjectPath $IndexRoot
$outputPath = Resolve-ProjectPath $OutputRoot
$rspnPath = Resolve-ProjectPath $RspnVpk
$installPath = Join-Path ([System.IO.Path]::GetFullPath($settings.ps4GameRoot)) 'vpk_ps4'

foreach ($requiredDirectory in @($sparsePath, $indexPath, $installPath)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) { throw "Required directory not found: $requiredDirectory" }
}
if (-not (Test-Path -LiteralPath $rspnPath -PathType Leaf)) { throw "RSPNVPK not found: $rspnPath" }

& (Join-Path $PSScriptRoot 'Apply-Stage1Compatibility.ps1') -Root $sparsePath | Out-Host

$targets = @('frontend', 'mp_common', 'mp_glitch')
$builtFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()

foreach ($target in $targets) {
    $sourceDirectory = Join-Path $sparsePath $target
    $directoryName = "englishclient_$target.bsp.pak000_dir.vpk"
    $chunkName = "client_$target.bsp.pak000_228.vpk"
    $pristineIndex = Join-Path $indexPath $directoryName
    $targetOutput = Join-Path $outputPath $target

    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) { throw "Sparse payload not found for ${target}: $sourceDirectory" }
    if (-not (Test-Path -LiteralPath $pristineIndex -PathType Leaf)) { throw "Pristine PS4 index not found for ${target}: $pristineIndex" }

    New-Item -ItemType Directory -Path $targetOutput -Force | Out-Null
    Copy-Item -LiteralPath $pristineIndex -Destination (Join-Path $targetOutput $directoryName) -Force

    Push-Location $targetOutput
    try {
        & $rspnPath $directoryName -s -n 228 -d $sourceDirectory -o .
        $rspnExitCode = $LASTEXITCODE
    }
    finally { Pop-Location }
    if ($rspnExitCode -ne 0) { throw "RSPNVPK failed for $target with exit code $rspnExitCode" }

    foreach ($name in @($directoryName, $chunkName)) {
        $built = Get-Item -LiteralPath (Join-Path $targetOutput $name) -ErrorAction Stop
        if ($built.Length -eq 0) { throw "RSPNVPK produced an empty file: $($built.FullName)" }
        $builtFiles.Add($built)
    }
}

$manifest = foreach ($file in $builtFiles) {
    [pscustomobject]@{
        Target = $file.Directory.Name
        File = $file.Name
        Bytes = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifestPath = Join-Path $outputPath 'manifest.csv'
$manifest | Export-Csv -LiteralPath $manifestPath -NoTypeInformation

$backupPath = $null
if (-not $NoBackup) {
    $backupPath = Join-Path $repositoryRoot ('work\deploy-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
    foreach ($file in $builtFiles) {
        $installed = Join-Path $installPath $file.Name
        if (Test-Path -LiteralPath $installed -PathType Leaf) { Copy-Item -LiteralPath $installed -Destination $backupPath -Force }
    }
}

foreach ($file in $builtFiles) {
    $destination = Join-Path $installPath $file.Name
    if ($PSCmdlet.ShouldProcess($destination, "Install $($file.FullName)")) {
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        $builtHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($installedHash -ne $builtHash) { throw "Installed file verification failed: $destination" }
    }
}

[pscustomobject]@{
    InstalledTo = $installPath
    Files = $builtFiles.Count
    Manifest = $manifestPath
    Backup = $backupPath
}