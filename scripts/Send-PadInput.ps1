param([Parameter(Mandatory)] [ValidateSet('up','down','left','right','l1','r1','cross','circle')] [string] $Key, [int] $Times = 1, [int] $HoldMs = 120, [int] $GapMs = 350)
# Sends a pad button to shadPS4 through its keyboard mapping (input_config/default.ini)
# by posting key messages to the game window, without taking focus.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KeyPost {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
'@ -ErrorAction SilentlyContinue
$vk = @{ up = 0x26; down = 0x28; left = 0x25; right = 0x27; l1 = 0x51; r1 = 0x55; cross = 0x4E; circle = 0x42 }[$Key]
$extended = $Key -in 'up', 'down', 'left', 'right'
$proc = Get-Process shadPS4 -ErrorAction Stop | Select-Object -First 1
$windows = New-Object System.Collections.Generic.List[IntPtr]
[KeyPost]::EnumWindows({ param($h, $p)
    $pid2 = 0; [void][KeyPost]::GetWindowThreadProcessId($h, [ref]$pid2)
    if ($pid2 -eq $proc.Id -and [KeyPost]::IsWindowVisible($h)) { $windows.Add($h) }
    return $true }, [IntPtr]::Zero) | Out-Null
if ($windows.Count -eq 0) { throw 'No visible shadPS4 window' }
$scan = [KeyPost]::MapVirtualKey($vk, 0)
$flags = if ($extended) { 1 -shl 24 } else { 0 }
$down = [IntPtr](1 -bor ($scan -shl 16) -bor $flags)
$up = [IntPtr]([int64]1 -bor ($scan -shl 16) -bor $flags -bor (1 -shl 30) -bor ([int64]1 -shl 31))
for ($i = 0; $i -lt $Times; $i++) {
    foreach ($w in $windows) { [void][KeyPost]::PostMessage($w, 0x0100, [IntPtr]$vk, $down) }
    Start-Sleep -Milliseconds $HoldMs
    foreach ($w in $windows) { [void][KeyPost]::PostMessage($w, 0x0101, [IntPtr]$vk, $up) }
    Start-Sleep -Milliseconds $GapMs
}
