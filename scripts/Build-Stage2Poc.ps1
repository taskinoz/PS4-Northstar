[CmdletBinding()]
param(
    [string] $Toolchain = (Join-Path $PSScriptRoot '..\tools\openorbis-0.5.4\OpenOrbis\PS4Toolchain'),
    [string] $Output = (Join-Path $PSScriptRoot '..\dist\stage2-poc'),
    [switch] $EnableDiagnosticConVar,
    [switch] $EnableTeamChangesConVar,
    [switch] $EnableDiagnosticUiNative,
    [switch] $EnableM6FsOverlay,
    [switch] $EnableM6ModMetadata,
    [switch] $EnableM6ScriptProbe,
    [switch] $EnableM6ScriptInject
)
$ErrorActionPreference = 'Stop'
$toolchainRoot = [IO.Path]::GetFullPath($Toolchain)
$outputRoot = [IO.Path]::GetFullPath($Output)
$env:OO_PS4_TOOLCHAIN = $toolchainRoot
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\native\stage2'))
$clang = (Get-Command clang++.exe -ErrorAction Stop).Source
$lld = (Get-Command ld.lld.exe -ErrorAction Stop).Source
$converter = Join-Path $toolchainRoot 'bin\windows\create-fself.exe'
foreach ($path in @($toolchainRoot, $converter, (Join-Path $toolchainRoot 'lib\crtlib.o'))) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required OpenOrbis path not found: $path" }
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$object = Join-Path $outputRoot 'module.o'
$runtimeObject = Join-Path $outputRoot 'runtime.o'
$elf = Join-Path $outputRoot 'northstar_ps4.elf'
$oelf = Join-Path $outputRoot 'northstar_ps4.oelf'
$prx = Join-Path $outputRoot 'northstar_ps4.prx'
$compileArgs = @('--target=x86_64-pc-freebsd12-elf','-fPIC','-funwind-tables','-fno-exceptions','-fno-rtti','-c','-isysroot',$toolchainRoot,'-isystem',(Join-Path $toolchainRoot 'include'),'-isystem',(Join-Path $toolchainRoot 'include\c++\v1'),'-I',(Join-Path $sourceRoot 'include'),'-o',$object,(Join-Path $sourceRoot 'src\module.cpp'))
& $clang @compileArgs
if ($LASTEXITCODE) { throw "OpenOrbis compile failed: $LASTEXITCODE" }
$runtimeCompileArgs = $compileArgs.Clone()
$runtimeCompileArgs[$runtimeCompileArgs.Count - 2] = $runtimeObject
$runtimeCompileArgs[$runtimeCompileArgs.Count - 1] = Join-Path $sourceRoot 'src\runtime.cpp'
if ($EnableDiagnosticConVar) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_DIAGNOSTIC_CONVAR=1') + $runtimeCompileArgs
}
if ($EnableTeamChangesConVar) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_TEAM_CHANGES_CONVAR=1') + $runtimeCompileArgs
}
if ($EnableDiagnosticUiNative) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_DIAGNOSTIC_UI_NATIVE=1') + $runtimeCompileArgs
}
if ($EnableM6FsOverlay) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_M6_FS_OVERLAY=1') + $runtimeCompileArgs
}
if ($EnableM6ModMetadata) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_M6_MOD_METADATA=1') + $runtimeCompileArgs
}
if ($EnableM6ScriptProbe) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_M6_SCRIPT_PROBE=1') + $runtimeCompileArgs
}
if ($EnableM6ScriptInject) {
    $runtimeCompileArgs = @('-DNORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT=1') + $runtimeCompileArgs
}
& $clang @runtimeCompileArgs
if ($LASTEXITCODE) { throw "OpenOrbis runtime compile failed: $LASTEXITCODE" }
$linkArgs = @('-m','elf_x86_64','-pie','--script',(Join-Path $sourceRoot 'link.x'),'--eh-frame-hdr','-L',(Join-Path $toolchainRoot 'lib'),$object,$runtimeObject,'-lc','-lc++','-lkernel',(Join-Path $toolchainRoot 'lib\crtlib.o'),'-o',$elf)
& $lld @linkArgs
if ($LASTEXITCODE) { throw "OpenOrbis link failed: $LASTEXITCODE" }
# shadPS4 starts PRX DT_INIT but does not currently invoke OpenOrbis's hidden module_start.
# Route DT_INIT to the same idempotent initializer retained in .init_array for real hardware.
$nm = (Get-Command llvm-nm.exe -ErrorAction Stop).Source
$initSymbol = (& $nm -an $elf | Select-String '\bNorthstarPs4Init$' | Select-Object -First 1).Line
if (-not $initSymbol) { throw 'NorthstarPs4Init was not found in the linked ELF.' }
$initAddress = [Convert]::ToUInt64(($initSymbol -split '\s+')[0], 16)
$elfBytes = [IO.File]::ReadAllBytes($elf)
$phoff = [BitConverter]::ToUInt64($elfBytes, 0x20)
$phentsize = [BitConverter]::ToUInt16($elfBytes, 0x36)
$phnum = [BitConverter]::ToUInt16($elfBytes, 0x38)
$dynamicOffset = $null
$dynamicSize = 0
for ($i = 0; $i -lt $phnum; $i++) {
    $off = [int]($phoff + ($i * $phentsize))
    if ([BitConverter]::ToUInt32($elfBytes, $off) -eq 2) {
        $dynamicOffset = [BitConverter]::ToUInt64($elfBytes, $off + 8)
        $dynamicSize = [BitConverter]::ToUInt64($elfBytes, $off + 32)
        break
    }
}
if ($null -eq $dynamicOffset) { throw 'PT_DYNAMIC was not found in the linked ELF.' }
$patchedInit = $false
for ($off = [int]$dynamicOffset; $off -lt ($dynamicOffset + $dynamicSize); $off += 16) {
    if ([BitConverter]::ToUInt64($elfBytes, $off) -eq 12) {
        [Array]::Copy([BitConverter]::GetBytes([uint64]$initAddress), 0, $elfBytes, $off + 8, 8)
        $patchedInit = $true
        break
    }
}
if (-not $patchedInit) { throw 'DT_INIT was not found in the linked ELF.' }
[IO.File]::WriteAllBytes($elf, $elfBytes)
& $converter "-in=$elf" "-out=$oelf" "--lib=$prx" '--paid' '0x3800000000000011'
if ($LASTEXITCODE) { throw "OpenOrbis PRX conversion failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $prx -PathType Leaf)) { throw "OpenOrbis did not produce $prx" }
$file = Get-Item -LiteralPath $prx
[pscustomobject]@{ File=$file.FullName; Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); Toolchain=$toolchainRoot }
