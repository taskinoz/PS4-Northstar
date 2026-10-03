# Draws the token helper's icon source (app-icon.png, 1024x1024): a four-point
# north star on a PlayStation-blue tile. `bun tauri icon app-icon.png` makes
# every size from it into src-tauri/icons. Kept so the icon can be redrawn.
Add-Type -AssemblyName System.Drawing
$size = 1024
$bitmap = New-Object Drawing.Bitmap $size, $size
$g = [Drawing.Graphics]::FromImage($bitmap)
$g.SmoothingMode = 'AntiAlias'
$g.Clear([Drawing.Color]::Transparent)

# The tile: a rounded square, deep blue at the bottom to PlayStation blue at the top.
$inset = 64; $radius = 210; $edge = $size - $inset; $d = $radius * 2
$tile = New-Object Drawing.Drawing2D.GraphicsPath
$tile.AddArc($inset, $inset, $d, $d, 180, 90); $tile.AddArc($edge - $d, $inset, $d, $d, 270, 90)
$tile.AddArc($edge - $d, $edge - $d, $d, $d, 0, 90); $tile.AddArc($inset, $edge - $d, $d, $d, 90, 90); $tile.CloseFigure()
$fill = New-Object Drawing.Drawing2D.LinearGradientBrush((New-Object Drawing.Point 0, $inset), (New-Object Drawing.Point 0, $edge),
    [Drawing.Color]::FromArgb(0, 112, 209), [Drawing.Color]::FromArgb(4, 22, 66))
$g.FillPath($fill, $tile)

# A soft glow behind the star.
$glow = New-Object Drawing.Drawing2D.GraphicsPath
$glow.AddEllipse(232, 232, 560, 560)
$glowBrush = New-Object Drawing.Drawing2D.PathGradientBrush($glow)
$glowBrush.CenterColor = [Drawing.Color]::FromArgb(150, 120, 200, 255)
$glowBrush.SurroundColors = @([Drawing.Color]::FromArgb(0, 120, 200, 255))
$g.FillPath($glowBrush, $glow)

# The north star: four long points with narrow waists.
function Star([float] $cx, [float] $cy, [float] $long, [float] $waist) {
    $points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
    for ($i = 0; $i -lt 8; $i++) {
        $angle = [Math]::PI / 4 * $i - [Math]::PI / 2
        $r = if ($i % 2 -eq 0) { $long } else { $waist }
        $points.Add((New-Object Drawing.PointF(($cx + $r * [Math]::Cos($angle)), ($cy + $r * [Math]::Sin($angle)))))
    }
    $points.ToArray()
}
$g.FillPolygon([Drawing.Brushes]::White, (Star 512 512 330 70))
$small = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(210, 255, 255, 255))
$g.FillPolygon($small, (Star 760 290 62 14))
$g.Dispose()
$bitmap.Save((Join-Path $PSScriptRoot 'app-icon.png'), [Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
