$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture = Join-Path $root ('work\install-status-tests\' + [guid]::NewGuid().ToString('N'))
$binaryDir = Join-Path $fixture 'bin\ps4_retail'
New-Item -ItemType Directory -Path $binaryDir -Force | Out-Null
$check = Join-Path $root 'scripts\Get-NorthstarInstallStatus.ps1'
function Assert($condition, $message) { if (-not $condition) { throw $message } }
$status = & $check -GameRoot $fixture
Assert ($status.RuntimeMode -eq 'Missing PRX') 'Missing PRX was not identified.'
$prx = Join-Path $binaryDir 'northstar_ps4.prx'
[IO.File]::WriteAllText($prx, 'test fixture, not executable')
$hash = (Get-FileHash -LiteralPath $prx).Hash.ToLowerInvariant()
$status = & $check -GameRoot $fixture
Assert ($status.RuntimeMode -like 'Unknown build*') 'Unknown PRX was misidentified.'
$infoPath = Join-Path $fixture 'build.json'
$info = @{ schemaVersion=1; prxSha256=$hash; runtimeManifest=$false; filesystemOverrides=$false; lateScriptInjection=$false; authentication='disabled' }
$info | ConvertTo-Json | Set-Content -LiteralPath $infoPath
$status = & $check -GameRoot $fixture -BuildInfo $infoPath
Assert ($status.RuntimeMode -eq 'Bootstrap only (mods not loaded)') 'Bootstrap was misidentified.'
Assert (-not $status.FullCompatibilityVerified) 'Diagnostic claimed unverified compatibility.'
$info.runtimeManifest = $true
$info | ConvertTo-Json | Set-Content -LiteralPath $infoPath
$status = & $check -GameRoot $fixture -BuildInfo $infoPath
Assert ($status.RuntimeMode -eq 'Experimental native mod loader') 'Native loader was misidentified.'
$info.prxSha256 = 'wrong'
$info | ConvertTo-Json | Set-Content -LiteralPath $infoPath
$rejected = $false
try { & $check -GameRoot $fixture -BuildInfo $infoPath | Out-Null } catch { $rejected = $true }
Assert $rejected 'Mismatched build information was accepted.'
Assert ((Get-FileHash -LiteralPath $prx).Hash.ToLowerInvariant() -eq $hash) 'Diagnostic modified the PRX.'
Write-Host 'Installation-status tests passed.'
