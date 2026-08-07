[CmdletBinding()]
param(
    [string]$Config = (Join-Path $PSScriptRoot '..\config\local.json'),
    [string]$Profile = (Join-Path $PSScriptRoot '..\config\stage2-offsets-CUSA04013.json')
)
$ErrorActionPreference = 'Stop'

function Convert-HexUInt64([string]$Value) {
    if ($Value -notmatch '^0x[0-9a-fA-F]+$') { throw "Invalid hexadecimal value: $Value" }
    [Convert]::ToUInt64($Value.Substring(2), 16)
}

function Convert-HexBytes([string]$Value) {
    if (($Value.Length % 2) -ne 0 -or $Value -notmatch '^[0-9a-fA-F]+$') { throw "Invalid hexadecimal byte string: $Value" }
    $result = [byte[]]::new($Value.Length / 2)
    for ($i = 0; $i -lt $result.Length; $i++) { $result[$i] = [Convert]::ToByte($Value.Substring($i * 2, 2), 16) }
    $result
}

$settings = Get-Content -Raw -LiteralPath ([IO.Path]::GetFullPath($Config)) | ConvertFrom-Json
$profileData = Get-Content -Raw -LiteralPath ([IO.Path]::GetFullPath($Profile)) | ConvertFrom-Json
$enginePath = Join-Path ([IO.Path]::GetFullPath($settings.ps4GameRoot)) 'bin\ps4_retail\engine.prx'
if (-not (Test-Path -LiteralPath $enginePath -PathType Leaf)) { throw "Engine PRX not found: $enginePath" }

$actualHash = (Get-FileHash -LiteralPath $enginePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $profileData.sha256) { throw "Engine SHA-256 mismatch. Expected $($profileData.sha256), got $actualHash" }

$bytes = [IO.File]::ReadAllBytes($enginePath)
$rxFileOffset = Convert-HexUInt64 $profileData.image.rxFileOffset
$rxVirtualAddress = Convert-HexUInt64 $profileData.image.rxVirtualAddress
$results = foreach ($property in $profileData.anchors.PSObject.Properties) {
    $anchor = $property.Value
    $virtualAddress = Convert-HexUInt64 $anchor.virtualAddress
    $expected = @(Convert-HexBytes $anchor.preimage)
    $fileOffset = $rxFileOffset + ($virtualAddress - $rxVirtualAddress)
    if (($fileOffset + $expected.Count) -gt $bytes.Length) { throw "$($property.Name) falls outside engine.prx" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if ($bytes[$fileOffset + $i] -ne $expected[$i]) { throw ('{0} preimage mismatch at file offset 0x{1:X}' -f $property.Name, ($fileOffset + $i)) }
    }
    [pscustomobject]@{ Anchor=$property.Name; VirtualAddress=('0x{0:x}' -f $virtualAddress); FileOffset=('0x{0:x}' -f $fileOffset); PreimageBytes=$expected.Count; Verified=$true }
}

[pscustomobject]@{ Build=$profileData.build; Engine=$enginePath; SHA256=$actualHash; Status=$profileData.status; Anchors=@($results) }
