param([Parameter(Mandatory)] [string] $Out)
# Captures only the shadPS4 game window (PrintWindow, so it works when covered).
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
}
'@
$proc = Get-Process shadPS4 -ErrorAction Stop | Select-Object -First 1
$best = [IntPtr]::Zero; $bestArea = 0
[Win]::EnumWindows({ param($h, $p)
    $pid2 = 0; [void][Win]::GetWindowThreadProcessId($h, [ref]$pid2)
    if ($pid2 -eq $proc.Id -and [Win]::IsWindowVisible($h)) {
        $r = New-Object Win+RECT; [void][Win]::GetClientRect($h, [ref]$r)
        $area = ($r.R - $r.L) * ($r.B - $r.T)
        if ($area -gt $script:bestArea) { $script:bestArea = $area; $script:best = $h }
    }
    return $true }, [IntPtr]::Zero) | Out-Null
if ($best -eq [IntPtr]::Zero) { throw 'No visible shadPS4 window' }
$rect = New-Object Win+RECT; [void][Win]::GetClientRect($best, [ref]$rect)
$bmp = New-Object System.Drawing.Bitmap ($rect.R - $rect.L), ($rect.B - $rect.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
$ok = [Win]::PrintWindow($best, $dc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"captured $($rect.R - $rect.L)x$($rect.B - $rect.T) ok=$ok"
