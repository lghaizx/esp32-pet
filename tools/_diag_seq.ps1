# _diag_seq.ps1 - render whole strings and print the ink columns, so the
# GDI+ advance/origin geometry can be read off directly.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Render([string]$s, [string]$fontName, [int]$size) {
  $W = 240; $H = 32
  $font = New-Object System.Drawing.Font($fontName, $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $bmp  = New-Object System.Drawing.Bitmap $W, $H
  $g    = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $g.DrawString($s, $font, [System.Drawing.Brushes]::White, 0.0, 0.0)
  $g.Dispose()

  # ink columns -> contiguous runs
  $cols = @()
  for ($x = 0; $x -lt $W; $x++) {
    $on = $false
    for ($y = 0; $y -lt $H; $y++) { if ($bmp.GetPixel($x, $y).R -gt 127) { $on = $true; break } }
    $cols += $on
  }
  $runs = @()
  $st = -1
  for ($x = 0; $x -lt $W; $x++) {
    if ($cols[$x] -and $st -lt 0) { $st = $x }
    if ((-not $cols[$x]) -and $st -ge 0) { $runs += "$st..$($x-1)"; $st = -1 }
  }
  $bmp.Dispose(); $font.Dispose()
  return $runs
}

function CS([int[]]$cps) { -join ($cps | ForEach-Object { [string][char]$_ }) }

$tests = @(
  @('A',        @(0x41)),
  @('AA',       @(0x41, 0x41)),
  @('AAA',      @(0x41, 0x41, 0x41)),
  @('Wm',       @(0x57, 0x6D)),
  @('in',       @(0x69, 0x6E)),
  @('ni',       @(0x4F60)),                    # U+4F60
  @('ni+hao',   @(0x4F60, 0x597D)),            # U+4F60 U+597D
  @('hao+gan',  @(0x597D, 0x611F)),            # U+597D U+611F
  @('ni+a+hao', @(0x4F60, 0x61, 0x597D)),
  @('4CJK',     @(0x4E2D, 0x56FD, 0x4EBA, 0x6C11)),
  @('guo',      @(0x56FD)),
  @('ri',       @(0x65E5)),
  @('kou',      @(0x53E3)),
  @('tian',     @(0x7530))
)
foreach ($t in $tests) {
  $s = CS $t[1]
  $r = Render $s 'SimSun' 16
  Write-Host ("{0,-10} (U+{1}) runs: {2}" -f $t[0], (($t[1] | ForEach-Object { '{0:X4}' -f $_ }) -join ' '), ($r -join '  '))
}
