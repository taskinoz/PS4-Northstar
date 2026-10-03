<#
.SYNOPSIS
Builds the token helper: NorthstarPS4TokenHelper.exe (window) and NorthstarPS4TokenHelperCli.exe (terminal).

.DESCRIPTION
Compiles token-helper/src with the C# compiler of the .NET Framework 4.x that
ships with Windows 10 and 11 (C# 5), so nothing needs installing to build it
or to run it. The CLI build defines CLI (see Program.cs). The icon is drawn
here and written to the output folder, not kept in the repository.
#>
[CmdletBinding()]
param([string] $Output = (Join-Path (Split-Path $PSScriptRoot) 'dist\token-helper'))
$ErrorActionPreference = 'Stop'
$root = Join-Path (Split-Path $PSScriptRoot) 'token-helper'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) { $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe' }
if (-not (Test-Path $csc)) { throw 'The .NET Framework 4 C# compiler (csc.exe) was not found.' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path

# The icon: an "N" on a rounded square, as PNG images in an .ico container.
Add-Type -AssemblyName System.Drawing
$icon = Join-Path $Output 'NorthstarPS4TokenHelper.ico'
$images = foreach ($size in 16, 24, 32, 48, 64, 256) {
    $bitmap = New-Object Drawing.Bitmap $size, $size
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $radius = [Math]::Max(2, [int]($size * 0.2))
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2; $e = $size - 1
    $path.AddArc(0, 0, $d, $d, 180, 90); $path.AddArc($e - $d, 0, $d, $d, 270, 90)
    $path.AddArc($e - $d, $e - $d, $d, $d, 0, 90); $path.AddArc(0, $e - $d, $d, $d, 90, 90); $path.CloseFigure()
    $brush = New-Object Drawing.Drawing2D.LinearGradientBrush((New-Object Drawing.Point 0, 0), (New-Object Drawing.Point 0, $size),
        [Drawing.Color]::FromArgb(232, 96, 32), [Drawing.Color]::FromArgb(176, 44, 20))
    $g.FillPath($brush, $path)
    $font = New-Object Drawing.Font('Segoe UI', [float]($size * 0.62), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $format = New-Object Drawing.StringFormat
    $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
    $g.DrawString('N', $font, [Drawing.Brushes]::White, (New-Object Drawing.RectangleF(0, ($size * 0.02), $size, $size)), $format)
    $g.Dispose()
    $stream = New-Object IO.MemoryStream
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    , @($size, $stream.ToArray())
}
$file = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $side = if ($image[0] -ge 256) { 0 } else { $image[0] }
    $writer.Write([byte]$side); $writer.Write([byte]$side); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$image[1].Length); $writer.Write([uint32]$offset)
    $offset += $image[1].Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image[1]) }
$writer.Flush()
[IO.File]::WriteAllBytes($icon, $file.ToArray())

$sources = Get-ChildItem (Join-Path $root 'src') -Filter *.cs | ForEach-Object { $_.FullName }
$common = @('/nologo', '/optimize+', '/warnaserror+', '/platform:anycpu', "/win32manifest:$(Join-Path $root 'app.manifest')",
    "/win32icon:$icon", '/r:System.dll', '/r:System.Drawing.dll', '/r:System.Windows.Forms.dll', '/r:System.Xml.dll')
foreach ($build in @(
        @{ name = 'NorthstarPS4TokenHelper.exe'; options = @('/target:winexe') },
        @{ name = 'NorthstarPS4TokenHelperCli.exe'; options = @('/target:exe', '/define:CLI') })) {
    $out = Join-Path $Output $build.name
    & $csc @common @($build.options) "/out:$out" @sources
    if ($LASTEXITCODE) { throw "csc failed for $($build.name)" }
}
Remove-Item -LiteralPath $icon
Get-ChildItem $Output -Filter *.exe | ForEach-Object { '{0}  {1:N0} bytes' -f $_.Name, $_.Length }
