# =====================================================================
#  dump_lvgl_glyph.ps1  -  decode glyphs out of the generated LVGL font
#
#  Prints the raw 1-bpp bitmaps exactly as LVGL's fmt_txt reader would
#  see them, so you can eyeball that a glyph was packed correctly.
#
#     powershell -ExecutionPolicy Bypass -File tools/dump_lvgl_glyph.ps1 0x41 0x597D
# =====================================================================
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Codes = @('0x41', '0x597D'))
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'src\lv_font_cn.c'
$text = [System.IO.File]::ReadAllText($src)

# ---- parse the bitmap blob -------------------------------------------
$m = [regex]::Match($text, 'static const uint8_t cn_glyph_bitmap\[\]\s*=\s*\{(?<b>.*?)\};', 'Singleline')
$bytes = [byte[]](@($m.Groups['b'].Value -split "[,`r`n]" | Where-Object { $_ -match '\d' } | ForEach-Object { [int]$_.Trim() }))

# ---- parse the glyph descriptors -------------------------------------
$dsc = @()
foreach ($line in ($text -split "`n")) {
  $mm = [regex]::Match($line, '\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)')
  if ($mm.Success) {
    $dsc += , @([int]$mm.Groups[1].Value, [int]$mm.Groups[2].Value, [int]$mm.Groups[3].Value,
                [int]$mm.Groups[4].Value, [int]$mm.Groups[5].Value, [int]$mm.Groups[6].Value)
  }
}

# ---- parse the sparse CJK list ---------------------------------------
$cl  = [regex]::Match($text, 'static const uint16_t cn_cjk_list\[\d+\]\s*=\s*\{(?<l>.*?)\};', 'Singleline')
$cjk = @($cl.Groups['l'].Value -split ',' | Where-Object { $_ -match '0x' } | ForEach-Object { [Convert]::ToInt32($_.Trim(), 16) })

Write-Host ("bitmap = {0} bytes, {1} glyph descriptors, {2} CJK entries" -f $bytes.Count, $dsc.Count, $cjk.Count)

foreach ($code in $Codes) {
  $cp = [Convert]::ToInt32(($code -replace '^0[xX]', '').Trim(), 16)
  if ($cp -le 0x7E) { $id = 1 + ($cp - 0x20) }
  else {
    $i = [array]::IndexOf($cjk, $cp)
    if ($i -lt 0) { Write-Host ("U+{0:X4} : not in font" -f $cp); continue }
    $id = 96 + $i
  }
  $bi = $dsc[$id][0]; $bw = $dsc[$id][2]; $bh = $dsc[$id][3]
  Write-Host ("--- U+{0:X4}  id {1}  box {2}x{3}  bitmap_index {4}  adv_w {5}  ofs {6},{7}" -f `
      $cp, $id, $bw, $bh, $bi, $dsc[$id][1], $dsc[$id][4], $dsc[$id][5])
  # same walk as lv_draw_sw_letter.c: rows are one continuous bit stream
  for ($y = 0; $y -lt $bh; $y++) {
    $row = ''
    for ($x = 0; $x -lt $bw; $x++) {
      $bit  = $y * $bw + $x
      $byte = $bytes[$bi + [int][Math]::Floor($bit / 8)]
      $b    = ($byte -shr (7 - ($bit % 8))) -band 1
      $row += $(if ($b) { '#' } else { '.' })
    }
    Write-Host $row
  }
}
