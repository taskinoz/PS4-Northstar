[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameRoot = 'D:\PS4\ShadPS4\CUSA04013',
    [switch]$Disable
)

$ErrorActionPreference = 'Stop'
$expectedVanillaHash = '590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824'
$ebootPath = Join-Path $GameRoot 'eboot.bin'
$backupPath = Join-Path $GameRoot 'eboot.bin.northstar-stage2.bak'
$prxPath = Join-Path $GameRoot 'bin\ps4_retail\northstar_ps4.prx'
$manifestPath = Join-Path $PSScriptRoot '..\dist\stage2-poc\bootstrap-manifest.json'

if ($Disable) {
    if (-not (Test-Path -LiteralPath $backupPath)) { throw "Backup not found: $backupPath" }
    if ($PSCmdlet.ShouldProcess($ebootPath, 'Restore the retail eboot backup')) {
        Copy-Item -LiteralPath $backupPath -Destination $ebootPath -Force
        Write-Host "Restored: $ebootPath"
    }
    return
}

if (-not (Test-Path -LiteralPath $prxPath)) { throw "PoC PRX not found: $prxPath" }
$actualHash = (Get-FileHash -LiteralPath $ebootPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedVanillaHash) {
    throw "Refusing to patch unknown eboot. Expected $expectedVanillaHash, got $actualHash. Use -Disable to restore the recorded backup."
}

$bytes = [IO.File]::ReadAllBytes($ebootPath)
function Assert-Bytes([int]$Offset, [byte[]]$Expected) {
    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($bytes[$Offset + $i] -ne $Expected[$i]) {
            throw ('Preimage mismatch at file offset 0x{0:X}: expected {1:X2}, got {2:X2}' -f ($Offset + $i), $Expected[$i], $bytes[$Offset + $i])
        }
    }
}

# ELF virtual address 0x128d maps to file offset 0x528d in the RX LOAD segment.
Assert-Bytes 0x528d ([byte[]](0xE8,0x8E,0xED,0xFF,0xFF)) # call 0x20

# Locate the first executable PT_LOAD rather than assuming a program-header index.
$phoff = [BitConverter]::ToUInt64($bytes, 0x20)
$phentsize = [BitConverter]::ToUInt16($bytes, 0x36)
$phnum = [BitConverter]::ToUInt16($bytes, 0x38)
$rxPh = $null
for ($i = 0; $i -lt $phnum; $i++) {
    $off = [int]($phoff + ($i * $phentsize))
    $type = [BitConverter]::ToUInt32($bytes, $off)
    $flags = [BitConverter]::ToUInt32($bytes, $off + 4)
    $fileOffset = [BitConverter]::ToUInt64($bytes, $off + 8)
    if ($type -eq 1 -and $flags -eq 5 -and $fileOffset -eq 0x4000) { $rxPh = $off; break }
}
if ($null -eq $rxPh) { throw 'Executable PT_LOAD was not found.' }
if ([BitConverter]::ToUInt64($bytes, $rxPh + 0x20) -ne 0x29dc) { throw 'Unexpected RX PT_LOAD file size.' }
if ([BitConverter]::ToUInt64($bytes, $rxPh + 0x28) -ne 0x29dc) { throw 'Unexpected RX PT_LOAD memory size.' }

$stubVa = 0x29e0
$stubFileOffset = 0x4000 + $stubVa
$path = [Text.Encoding]::ASCII.GetBytes('/app0/bin/ps4_retail/northstar_ps4.prx' + [char]0)
$code = [Collections.Generic.List[byte]]::new()
$code.AddRange([byte[]](0x48,0x83,0xEC,0x08))                         # sub rsp, 8
$callInitNext = $stubVa + $code.Count + 5
$code.Add(0xE8); $code.AddRange([BitConverter]::GetBytes([int](0x20 - $callInitNext)))
$leaStart = $stubVa + $code.Count
$pathVa = $stubVa + 38
$code.AddRange([byte[]](0x48,0x8D,0x3D)); $code.AddRange([BitConverter]::GetBytes([int]($pathVa - ($leaStart + 7))))
$code.AddRange([byte[]](0x31,0xF6,0x31,0xD2,0x31,0xC9,0x45,0x31,0xC0,0x45,0x31,0xC9))
$callLoaderNext = $stubVa + $code.Count + 5
$code.Add(0xE8); $code.AddRange([BitConverter]::GetBytes([int](0x14e0 - $callLoaderNext)))
$code.AddRange([byte[]](0x48,0x83,0xC4,0x08,0xC3))
if ($code.Count -ne 38) { throw "Internal stub-size error: $($code.Count)" }
$payload = $code.ToArray() + $path
$newSegmentSize = $stubVa + $payload.Length
if ($newSegmentSize -ge 0x4000) { throw 'Bootstrap payload exceeds the executable segment gap.' }
for ($i = 0; $i -lt $payload.Length; $i++) {
    if ($bytes[$stubFileOffset + $i] -ne 0) { throw ('RX gap is not empty at 0x{0:X}' -f ($stubFileOffset + $i)) }
}

if (-not (Test-Path -LiteralPath $backupPath)) { Copy-Item -LiteralPath $ebootPath -Destination $backupPath }
[Array]::Copy($payload, 0, $bytes, $stubFileOffset, $payload.Length)
[Array]::Copy([BitConverter]::GetBytes([uint64]$newSegmentSize), 0, $bytes, $rxPh + 0x20, 8)
[Array]::Copy([BitConverter]::GetBytes([uint64]$newSegmentSize), 0, $bytes, $rxPh + 0x28, 8)
$callNext = 0x128d + 5
[Array]::Copy(([byte[]](0xE8) + [BitConverter]::GetBytes([int]($stubVa - $callNext))), 0, $bytes, 0x528d, 5)

$distPath = Join-Path $PSScriptRoot '..\dist\stage2-poc\eboot.stage2.bin'
[IO.Directory]::CreateDirectory((Split-Path $distPath)) | Out-Null
[IO.File]::WriteAllBytes($distPath, $bytes)
$patchedHash = (Get-FileHash -LiteralPath $distPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($PSCmdlet.ShouldProcess($ebootPath, 'Install hash-locked Stage 2 bootstrap')) {
    Copy-Item -LiteralPath $distPath -Destination $ebootPath -Force
}

[ordered]@{
    VanillaSha256 = $expectedVanillaHash
    PatchedSha256 = $patchedHash
    Backup = $backupPath
    Prx = $prxPath
    LoaderPltVirtualAddress = '0x14e0'
    StubVirtualAddress = ('0x{0:x}' -f $stubVa)
} | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "Stage 2 bootstrap enabled. Patched SHA256: $patchedHash"
Write-Host "Rollback: .\scripts\Enable-Stage2Bootstrap.ps1 -Disable"
