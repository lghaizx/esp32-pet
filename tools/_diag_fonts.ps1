Add-Type -AssemblyName System.Drawing
$inst = New-Object System.Drawing.Text.InstalledFontCollection
Write-Host '=== installed families ==='
$inst.Families | ForEach-Object { $_.Name }

Write-Host ''
Write-Host '=== candidate metric probe (16px Pixel, CJK 0x597D) ==='
$cands = @('SimSun', 'NSimSun', 'SimHei', 'FangSong', 'KaiTi', 'Microsoft YaHei', 'Microsoft JhengHei',
  'MS Gothic', 'MingLiU', 'PMingLiU', 'DengXian', 'MingLiU-ExtB', 'Yu Gothic', 'Meiryo', 'Malgun Gothic',
  'Noto Sans CJK SC', 'Noto Sans SC', 'Source Han Sans SC', 'Arial Unicode MS', 'Arial', 'Consolas')
foreach ($name in $cands) {
  try {
    $f = New-Object System.Drawing.Font($name, 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  } catch { Write-Host ("{0,-22} : construct failed" -f $name); continue }
  $resolved = $f.FontFamily.Name
  $bmp = New-Object System.Drawing.Bitmap 48, 48
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $g.DrawString([string][char]0x597D, $f, [System.Drawing.Brushes]::White, 0, 0)
  $g.DrawString('A', $f, [System.Drawing.Brushes]::White, 0, 0)   # separate pass overwrites; do CJK only
  $g.Dispose()
  $x1 = 999; $x2 = -1; $y1 = 999; $y2 = -1
  for ($y = 0; $y -lt 48; $y++) {
    for ($x = 0; $x -lt 48; $x++) {
      if ($bmp.GetPixel($x, $y).R -gt 127) {
        if ($x -lt $x1) { $x1 = $x }; if ($x -gt $x2) { $x2 = $x }
        if ($y -lt $y1) { $y1 = $y }; if ($y -gt $y2) { $y2 = $y }
      }
    }
  }
  $bmp.Dispose()
  Write-Host ("{0,-22} resolved={1,-22} h={2,3} ink x={3}..{4} y={5}..{6}" -f $name, $resolved, $f.Height, $x1, $x2, $y1, $y2)
  $f.Dispose()
}
