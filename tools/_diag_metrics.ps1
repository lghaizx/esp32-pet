Add-Type -AssemblyName System.Drawing
$cjk = [string][char]0x597D    # built from a codepoint to keep this file pure ASCII
foreach ($name in @('SimSun', 'NSimSun', 'SimHei', 'MS Gothic', 'DengXian', 'KaiTi')) {
  foreach ($sz in @(12, 13, 14, 15, 16)) {
    $f = New-Object System.Drawing.Font($name, $sz, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $bmp = New-Object System.Drawing.Bitmap 64, 64
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $advCJK = $g.MeasureString(($cjk + $cjk), $f).Width - $g.MeasureString($cjk, $f).Width
    $advAsc = $g.MeasureString('AA', $f).Width - $g.MeasureString('A', $f).Width
    $g.Clear([System.Drawing.Color]::Black)
    $g.DrawString($cjk, $f, [System.Drawing.Brushes]::White, 0, 0)
    $x1 = 999; $x2 = -1; $y1 = 999; $y2 = -1
    for ($y = 0; $y -lt 64; $y++) {
      for ($x = 0; $x -lt 64; $x++) {
        if ($bmp.GetPixel($x, $y).R -gt 127) {
          if ($x -lt $x1) { $x1 = $x }; if ($x -gt $x2) { $x2 = $x }
          if ($y -lt $y1) { $y1 = $y }; if ($y -gt $y2) { $y2 = $y }
        }
      }
    }
    $g.Dispose(); $bmp.Dispose()
    $fam = $f.FontFamily
    '{0,-9} sz={1,2} h={2,3} lineSpc={3,4} em={4,4} asc={5,4} dsc={6,4} advCJK={7,6:N2} advASC={8,6:N2} ink x={9}..{10} y={11}..{12}' -f `
      $name, $sz, $f.Height, $fam.GetLineSpacing($f.Style), $fam.GetEmHeight($f.Style), `
      $fam.GetCellAscent($f.Style), $fam.GetCellDescent($f.Style), $advCJK, $advAsc, $x1, $x2, $y1, $y2 | Write-Host
    $f.Dispose()
  }
}

