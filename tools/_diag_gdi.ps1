# temp diagnostic: where does SimSun put the ink?
Add-Type -AssemblyName System.Drawing
$font = New-Object System.Drawing.Font('SimSun', 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
foreach ($cp in @(0x41, 0x597D, 0x611F, 0x20)) {
  $bmp = New-Object System.Drawing.Bitmap 32, 24
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $g.DrawString([string][char]$cp, $font, [System.Drawing.Brushes]::White, 0, 0)
  $g.Dispose()
  Write-Host ("--- U+{0:X4} drawn at (0,0) ---" -f $cp)
  for ($y = 0; $y -lt 24; $y++) {
    $r = ''
    for ($x = 0; $x -lt 32; $x++) {
      if ($bmp.GetPixel($x, $y).R -gt 127) { $r += '#' } else { $r += '.' }
    }
    Write-Host $r
  }
  $bmp.Dispose()
}
$font.Dispose()
$f2 = New-Object System.Drawing.Font('SimSun', 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
Write-Host ("font height={0} ascent={1} size={2}" -f $f2.Height, $f2.FontFamily.GetCellAscent($f2.Style), $f2.Size)
$f2.Dispose()
Write-Host ("installed SimSun? " + ((New-Object System.Drawing.Text.InstalledFontCollection).Families | Where-Object { $_.Name -eq 'SimSun' }).Name)
