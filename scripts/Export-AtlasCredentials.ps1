<#
.SYNOPSIS
Exports the Atlas identity from a running PC Northstar client for PS4 use.

.DESCRIPTION
Northstar's client authenticates with Atlas using its Origin token and receives
a player token in return. The PS4 build cannot do that exchange itself - it has
no Origin session - so the identity is taken from a PC client that already has
one, which is the plan of record for this port: the EA credential never leaves
the PC, and the console receives only the uid and the Northstar player token.

Both values are read from the running client:

- **uid** from `engine.dll + 0x13F8E688`, the `g_pLocalPlayerUserID` string that
  NorthstarLauncher itself reads. Verified against the uid the client logs at
  startup, so a wrong offset is caught rather than exported.
- **player token** by locating `MasterServerManager::m_sOwnClientAuthToken`. It
  is a `char[33]` holding 32 lowercase hex characters, and it sits directly
  after `m_sOwnServerId[33]` and `m_sOwnServerAuthToken[33]`, both empty on a
  client that is not hosting. Searching committed private writable memory for 32
  hex characters and a NUL preceded by 66 zero bytes matches that layout; on a
  client with one Atlas session there is exactly one such match.

The Origin token is **not** read, and nothing is sent anywhere. This script only
reads memory and writes a file.

.NOTES
The exported file contains a live credential for the account. Anyone holding it
can authenticate to Atlas as that player until it expires (24 hours by default,
or until the PC client authenticates again, which mints a replacement and
invalidates this one). It is written outside the repository for that reason -
do not commit it.

.PARAMETER Output
Where to write the exported identity. Defaults to the emulator's writable data
directory, which is what the PS4 build sees as /data/northstar_ps4.

.PARAMETER ShowToken
Print the token in full rather than masked. Off by default.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Output = (Join-Path $env:APPDATA 'shadPS4\data\northstar_ps4\atlas_identity.json'),
    [switch] $ShowToken
)
$ErrorActionPreference = 'Stop'

# g_pLocalPlayerUserID, the same address NorthstarLauncher reads.
$uidOffset = 0x13F8E688

$process = Get-Process -Name 'NorthstarLauncher' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw 'NorthstarLauncher is not running. Start PC Northstar and sign in first.' }
$engine = $process.Modules | Where-Object { $_.ModuleName -eq 'engine.dll' } | Select-Object -First 1
if (-not $engine) { throw 'engine.dll is not loaded in NorthstarLauncher yet; wait for the game to reach the menu.' }

$interop = @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class NsMem {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(int a, bool i, int p);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr b, byte[] buf, int size, out IntPtr read);
  [StructLayout(LayoutKind.Sequential)] public struct MBI {
    public IntPtr BaseAddress, AllocationBase; public int AllocationProtect; public IntPtr RegionSize;
    public int State, Protect, Type;
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);

  const int PROCESS_VM_READ = 0x0010, PROCESS_QUERY_INFORMATION = 0x0400;

  public static string ReadAsciiZ(int pid, long addr, int max) {
    IntPtr h = OpenProcess(PROCESS_VM_READ, false, pid);
    if (h == IntPtr.Zero) throw new Exception("OpenProcess failed: " + Marshal.GetLastWin32Error());
    byte[] buf = new byte[max]; IntPtr got;
    bool ok = ReadProcessMemory(h, new IntPtr(addr), buf, max, out got);
    CloseHandle(h);
    if (!ok) throw new Exception("ReadProcessMemory failed: " + Marshal.GetLastWin32Error());
    int n = Array.IndexOf(buf, (byte)0); if (n < 0) n = max;
    return System.Text.Encoding.ASCII.GetString(buf, 0, n);
  }

  static bool IsHex(byte b){ return (b>='0'&&b<='9')||(b>='a'&&b<='f'); }

  // m_sOwnClientAuthToken: 32 hex + NUL, preceded by two empty char[33] buffers.
  public static List<string> FindTokens(int pid) {
    var strong = new List<string>();
    IntPtr h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pid);
    if (h == IntPtr.Zero) throw new Exception("OpenProcess failed: " + Marshal.GetLastWin32Error());
    long addr = 0; MBI mbi;
    while (VirtualQueryEx(h, new IntPtr(addr), out mbi, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = mbi.RegionSize.ToInt64();
      if (size <= 0) break;
      bool usable = mbi.Type == 0x20000 && mbi.State == 0x1000 &&
                    ((mbi.Protect & 0x04) != 0 || (mbi.Protect & 0x40) != 0) &&
                    (mbi.Protect & 0x100) == 0 && size < 256L*1024*1024;
      if (usable) {
        byte[] buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, mbi.BaseAddress, buf, (int)size, out got)) {
          int n = (int)got.ToInt64();
          for (int i = 66; i + 33 <= n; i++) {
            if (buf[i+32] != 0) continue;
            bool hex = true;
            for (int k = 0; k < 32; k++) if (!IsHex(buf[i+k])) { hex = false; break; }
            if (!hex) continue;
            bool zeros = true;
            for (int k = i-66; k < i; k++) if (buf[k] != 0) { zeros = false; break; }
            if (zeros) strong.Add(System.Text.Encoding.ASCII.GetString(buf, i, 32));
          }
        }
      }
      addr = mbi.BaseAddress.ToInt64() + size;
      if (addr <= 0) break;
    }
    CloseHandle(h);
    return strong;
  }
}
"@
Add-Type -TypeDefinition $interop -Language CSharp

$uid = [NsMem]::ReadAsciiZ($process.Id, $engine.BaseAddress.ToInt64() + $uidOffset, 32)
if ($uid -notmatch '^[0-9]{6,20}$') {
    throw "Read '$uid' at the uid offset, which is not a uid. The offset is wrong for this Northstar build; do not export."
}

# Cross-check against what the client logged, so a plausible-looking but wrong
# read is still caught.
$logDir = Join-Path (Split-Path $process.Path -Parent) 'R2Northstar\logs'
$logged = $null
if (Test-Path -LiteralPath $logDir) {
    $newest = Get-ChildItem -LiteralPath $logDir -Filter 'nslog*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newest) {
        $line = Select-String -LiteralPath $newest.FullName -Pattern 'authenticate with northstar masterserver for user (\d+)' | Select-Object -Last 1
        if ($line) { $logged = $line.Matches[0].Groups[1].Value }
    }
}
if ($logged -and $logged -ne $uid) {
    throw "uid mismatch: memory says $uid, the client log says $logged. Refusing to export."
}
if (-not $logged) {
    Write-Warning 'Could not cross-check the uid against a client log; proceeding on the memory read alone.'
}

# Forced to an array: a single result would otherwise index as a string.
$tokens = @([NsMem]::FindTokens($process.Id) | Select-Object -Unique)
if ($tokens.Count -eq 0) {
    throw 'No player token found. Has the client finished authenticating? The log should say "Northstar origin authentication completed successfully".'
}
if ($tokens.Count -gt 1) {
    throw "Found $($tokens.Count) candidate tokens; refusing to guess. Restart the client and export again."
}
$token = $tokens[0]

$masked = $token.Substring(0,4) + ('*' * 24) + $token.Substring(28,4)
Write-Host ("uid   : " + $uid)
Write-Host ("token : " + $(if ($ShowToken) { $token } else { $masked }))

$payload = [ordered]@{
    schemaVersion = 1
    uid           = $uid
    playerToken   = $token
    exportedUtc   = (Get-Date).ToUniversalTime().ToString('o')
    # Atlas mints a replacement whenever the PC client authenticates again, and
    # the default lifetime is 24 hours.
    note          = 'Live Atlas credential. Do not commit or share. Re-export after the PC client re-authenticates.'
}
if ($PSCmdlet.ShouldProcess($Output, 'Write Atlas identity')) {
    New-Item -ItemType Directory -Path (Split-Path $Output -Parent) -Force | Out-Null
    $json = $payload | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText($Output, $json, [Text.UTF8Encoding]::new($false))
}

[pscustomobject]@{
    Uid        = $uid
    TokenMask  = $masked
    Output     = $Output
    CrossCheck = if ($logged) { 'log' } else { 'none' }
}
