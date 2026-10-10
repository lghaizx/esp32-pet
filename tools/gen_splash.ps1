# =====================================================================
#  gen_splash.ps1  -  build the boot picture that setup() pushes first
#
#  It renders / downscales a picture into the 160x128 RGB565 bitmap that
#  src/splash.cpp sends to the panel before the UI comes up, and writes
#  src/splash_data.cpp (the generated file is compiled in, so the picture
#  costs 160 * 128 * 2 = 40960 bytes of the app's flash slot).
#
#  Use your own artwork - any format GDI+ can open - either way round:
#     powershell -ExecutionPolicy Bypass -File tools/gen_splash.ps1 -Image C:\logo.png
#     copy C:\logo.png tools\splash.png      (then run the script bare)
#  With neither, the built-in pet-face artwork below is drawn, so the project
#  always has a boot picture and no image file has to be kept around.
#
#    -Fit cover     (default) fills the screen, cropping the overflow
#    -Fit contain   letterboxes the picture on the UI backdrop instead
#
#  Re-run after changing the picture, then rebuild and upload.
# =====================================================================
param(
  [string]$Image = '',
  [ValidateSet('cover', 'contain')][string]$Fit = 'cover',
  [string]$Out = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot          # project root
$W = 160; $H = 128                                # panel, landscape (SCR_ROTATION 3)
if (-not $Out) { $Out = Join-Path $root 'src\splash_data.cpp' }

# backdrop of the UI (COL_BG -> COL_FACE_BG) - the "contain" border colour
$BgTop = @(8, 10, 20)
$BgBot = @(16, 22, 44)

$bmp = [System.Drawing.Bitmap]::new($W, $H)
$g   = [System.Drawing.Graphics]::FromImage($bmp)

if (-not $Image) {
  $def = Join-Path $PSScriptRoot 'splash.png'
  if (Test-Path $def) { $Image = $def }
}

if ($Image) {
  # ---- 1a. a real picture: scale it to the panel ---------------------
  if (-not (Test-Path $Image)) { throw "picture not found: $Image" }
  $src = [System.Drawing.Image]::FromFile((Resolve-Path $Image).Path)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  if ($Fit -eq 'cover') {
    $scale = [Math]::Max($W / $src.Width, $H / $src.Height)
  } else {
    $scale = [Math]::Min($W / $src.Width, $H / $src.Height)
    $g.Clear([System.Drawing.Color]::FromArgb($BgTop[0], $BgTop[1], $BgTop[2]))
  }
  $dw = [int][Math]::Round($src.Width * $scale)
  $dh = [int][Math]::Round($src.Height * $scale)
  $dx = [int](($W - $dw) / 2); $dy = [int](($H - $dh) / 2)
  $g.DrawImage($src, [System.Drawing.Rectangle]::new($dx, $dy, $dw, $dh))
  $src.Dispose()
  Write-Host ("splash: {0} -> {1}x{2} ({3})" -f (Split-Path -Leaf $Image), $dw, $dh, $Fit)
} else {
  # ---- 1b. built-in artwork: the pet's face as a logo -----------------
  # Same elements the animated face is made of (see eyes.cpp): white sclera,
  # cyan iris, dark pupil, one highlight - so the boot picture is unmistakably
  # this pet and not a stranger's logo.
  $rect = [System.Drawing.Rectangle]::new(0, 0, $W, $H)
  $bg = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
    $rect,
    [System.Drawing.Color]::FromArgb($BgTop[0], $BgTop[1], $BgTop[2]),
    [System.Drawing.Color]::FromArgb($BgBot[0], $BgBot[1], $BgBot[2]),
    90.0)
  $g.FillRectangle($bg, $rect); $bg.Dispose()
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

  $sclera = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 255))
  $iris   = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(60, 190, 255))
  $pupil  = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(6, 8, 16))
  foreach ($cx in @(52, 108)) {
    $g.FillEllipse($sclera, $cx - 24, 8, 48, 54)     # eyeball
    $g.FillEllipse($iris,   $cx - 15, 22, 30, 30)    # iris
    $g.FillEllipse($pupil,  $cx - 6,  31, 12, 12)    # pupil
    $g.FillEllipse($sclera, $cx - 4,  24, 8, 8)      # highlight
  }
  $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(225, 235, 255), 3)
  $g.DrawArc($pen, 66, 60, 28, 22, 20, 140)          # smile
  $pen.Dispose()

  $f1 = [System.Drawing.Font]::new('Segoe UI', 13, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
  $f2 = [System.Drawing.Font]::new('Segoe UI', 7, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
  $acc = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(0, 200, 255))
  $txt = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(214, 224, 246))
  $sf = [System.Drawing.StringFormat]::new()
  $sf.Alignment = [System.Drawing.StringAlignment]::Center
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $g.DrawString('ESP32', $f1, $acc, [System.Drawing.RectangleF]::new(0, 90, $W, 18), $sf)
  $g.DrawString('P I X E L   P E T', $f2, $txt, [System.Drawing.RectangleF]::new(0, 107, $W, 12), $sf)
  $f1.Dispose(); $f2.Dispose(); $acc.Dispose(); $txt.Dispose(); $sf.Dispose()
  $sclera.Dispose(); $iris.Dispose(); $pupil.Dispose()
  Write-Host 'splash: built-in artwork (drop tools\splash.png or pass -Image to use your own)'
}
$g.Dispose()


# ---- 2. RGB565 + C++ ---------------------------------------------------
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// AUTO-GENERATED by tools/gen_splash.ps1  -  DO NOT EDIT BY HAND')
[void]$sb.AppendLine('// Regenerate with:  powershell -ExecutionPolicy Bypass -File tools/gen_splash.ps1')
[void]$sb.AppendLine('#include "config.h"')
[void]$sb.AppendLine('#include "splash.h"')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#if USE_SPLASH')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('// splash.h sizes the array the way the panel is laid out, so a mismatch is a')
[void]$sb.AppendLine('// bug and not a setting - catch it here instead of on the screen.')
[void]$sb.AppendLine('static_assert(SPLASH_W == SCR_W && SPLASH_H == SCR_H,')
[void]$sb.AppendLine('              "the splash bitmap must match the screen geometry");')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('const uint16_t splash_map[SPLASH_W * SPLASH_H] = {')

$row = New-Object System.Text.StringBuilder
$n = 0
for ($y = 0; $y -lt $H; $y++) {
  for ($x = 0; $x -lt $W; $x++) {
    $c = $bmp.GetPixel($x, $y)
    $v = ((($c.R -band 0xF8) -shl 8) -bor (($c.G -band 0xFC) -shl 3) -bor ($c.B -shr 3)) -band 0xFFFF
    [void]$row.Append(('0x{0:X4},' -f $v))
    $n++
    if ($n % 12 -eq 0) { [void]$sb.AppendLine('  ' + $row.ToString()); [void]$row.Clear() }
  }
}
if ($row.Length -gt 0) { [void]$sb.AppendLine('  ' + $row.ToString()) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#endif  // USE_SPLASH')

[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
$bmp.Dispose()
Write-Host ("splash: {0} pixels -> {1} ({2:N0} B of flash)" -f $n, $Out, ($n * 2))
