# temp diagnostic: render src/splash_data.cpp back into a picture, so the
# generated (RGB565 quantised) bitmap can be looked at instead of trusted.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'src\splash_data.cpp'
$txt  = [System.IO.File]::ReadAllText($src)
$W = 160; $H = 128
$vals = [regex]::Matches($txt, '0x([0-9A-Fa-f]{4})') | ForEach-Object { [Convert]::ToInt32($_.Groups[1].Value, 16) }
Write-Host ("splash values: {0} (expected {1})" -f $vals.Count, ($W * $H))
if ($vals.Count -ne $W * $H) { throw 'wrong value count' }

$bmp = [System.Drawing.Bitmap]::new($W, $H)
for ($i = 0; $i -lt $W * $H; $i++) {
  $v = $vals[$i]
  $r = [int](((($v -shr 11) -band 0x1F) * 255) / 31)
  $g = [int](((($v -shr 5) -band 0x3F) * 255) / 63)
  $b = [int]((($v -band 0x1F) * 255) / 31)
  $bmp.SetPixel(($i % $W), [int][Math]::Floor($i / $W), [System.Drawing.Color]::FromArgb($r, $g, $b))
}
$big = [System.Drawing.Bitmap]::new(($W * 4), ($H * 4))
$g2 = [System.Drawing.Graphics]::FromImage($big)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g2.DrawImage($bmp, 0, 0, $W * 4, $H * 4)
$g2.Dispose()
$out = Join-Path $root 'tools\_splash_preview.png'
$big.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$big.Dispose(); $bmp.Dispose()
Write-Host ("preview -> {0} (160x128 shown at 4x)" -f $out)
