# =====================================================================
#  gen_lvgl_font.ps1  -  build the LVGL font used by the LVGL user interface
#
#  It scans src/*.cpp + include/*.h for every character with a codepoint
#  > 0x7F (the Chinese UI text) and renders each one with a Windows CJK
#  font (SimSun). Printable ASCII (0x20..0x7E) is rendered too, so the
#  whole UI (Latin or Chinese) can be drawn with a single `lv_font_cn`.
#
#  How a glyph is packed (this matters - LVGL is picky):
#    * the string is rendered at (0,0), the ink bounding box is measured
#      and ONLY that box is stored (tight box, so ofs_x / ofs_y describe
#      exactly where the ink sits relative to the pen / baseline),
#    * GDI+ DrawString offsets every glyph to the right by a uniform
#      amount (`$InsetX` px). It is detected automatically as the minimum
#      ink-x over all glyphs and removed, so glyph bearings are correct,
#    * rows are stored as one continuous bit stream (box_w*bpp bits per
#      row, no byte padding per row) - that is what lv_draw_sw_letter.c
#      walks. Every glyph starts on a byte boundary (bitmap_index),
#    * adv_w is emitted in 8.4 fixed point (LVGL does (adv_w+8)>>4),
#      CJK is forced to a 16px advance so text keeps the classic 16px
#      grid of the hand drawn UI, ASCII keeps the font's own advance.
#
#  The output is a plain LVGL "fmt_txt" font (src/lv_font_cn.c):
#    * glyph 0            = invalid / not found
#    * glyphs 1..95       = ASCII 0x20..0x7E   (cmap FORMAT0_TINY)
#    * glyphs 96..        = CJK, sparse        (cmap SPARSE_TINY)
#
#  Re-run this whenever you add / change Chinese text in the sources:
#     powershell -ExecutionPolicy Bypass -File tools/gen_lvgl_font.ps1
#
#  Inspect a glyph afterwards with:
#     powershell -ExecutionPolicy Bypass -File tools/dump_lvgl_glyph.ps1 0x41 0x597D
# =====================================================================
param(
  [string]$FontName = 'SimSun',
  [int]$SizePx      = 16,      # font size in pixels
  [int]$InsetX      = -1,      # GDI+ left inset; -1 = auto detect
  [int]$CjkAdvPx    = 16,      # forced advance for full width glyphs
  [int]$AsciiAdvPx  = 8        # forced advance for ASCII (0 = use font)
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root    = Split-Path -Parent $PSScriptRoot          # project root
$srcdir  = Join-Path $root 'src'
$incdir  = Join-Path $root 'include'
$outFile = Join-Path $srcdir 'lv_font_cn.c'

# nominal cell sizes (only used for the sanity report below)
$CJK_W     = 16            # full-width cell for CJK
$ASCII_W   = 8             # half-width cell for ASCII

# ---- 1. collect every non-ASCII codepoint used in the sources ---------
$text = ''
Get-ChildItem $srcdir, $incdir -Include *.cpp, *.c, *.h -File -Recurse |
  Where-Object { $_.Name -ne 'cnfont_data.cpp' -and $_.Name -ne 'lv_font_cn.c' } |
  ForEach-Object { $text += [System.IO.File]::ReadAllText($_.FullName) + "`n" }

$codes = @{}
foreach ($ch in $text.ToCharArray()) {
  $cp = [int][char]$ch
  if ($cp -gt 0x7F -and $cp -ne 0xFEFF) { $codes[$cp] = $true }
}
$cjk = @($codes.Keys | Sort-Object)

# full glyph list: printable ASCII first, then the CJK codepoints
$ascii  = 0x20..0x7E
$glyphs = @($ascii) + @($cjk)

# ---- 2a. measure the ink box of every glyph ---------------------------
$font = New-Object System.Drawing.Font($FontName, $SizePx, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$hint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

$RW = 48; $RH = 32               # render canvas - deliberately generous
$probe = New-Object System.Drawing.Bitmap $RW, $RH
$gp    = [System.Drawing.Graphics]::FromImage($probe)
$gp.TextRenderingHint = $hint

$info = @()
foreach ($cp in $glyphs) {
  $isAscii = ($cp -le 0x7E)
  $s = [string][char]$cp
  # advance = width(two glyphs) - width(one glyph); the difference removes
  # the padding GDI+ adds to its layout box.
  $adv = $gp.MeasureString(($s + $s), $font).Width - $gp.MeasureString($s, $font).Width

  $bmp = New-Object System.Drawing.Bitmap $RW, $RH
  $g   = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = $hint
  $g.DrawString($s, $font, [System.Drawing.Brushes]::White, 0.0, 0.0)
  $g.Dispose()

  $x1 = 9999; $y1 = 9999; $x2 = -1; $y2 = -1
  for ($y = 0; $y -lt $RH; $y++) {
    for ($x = 0; $x -lt $RW; $x++) {
      if ($bmp.GetPixel($x, $y).R -gt 127) {
        if ($x -lt $x1) { $x1 = $x }; if ($x -gt $x2) { $x2 = $x }
        if ($y -lt $y1) { $y1 = $y }; if ($y -gt $y2) { $y2 = $y }
      }
    }
  }
  $info += (, ([pscustomobject]@{
    Cp = $cp; Ascii = $isAscii; Adv = $adv; Bmp = $bmp
    X1 = $x1; Y1 = $y1; X2 = $x2; Y2 = $y2; Empty = ($x2 -lt 0)
  }))
}
$gp.Dispose(); $probe.Dispose()

# ---- 2b. calibrate GDI+ geometry --------------------------------------
$inked  = @($info | Where-Object { -not $_.Empty })
$minX   = ($inked | ForEach-Object { $_.X1 } | Measure-Object -Minimum).Minimum
if ($InsetX -lt 0) { $InsetX = [int]$minX }
$maxBot = ($inked | ForEach-Object { $_.Y2 + 1 } | Measure-Object -Maximum).Maximum
$lineH  = [Math]::Max($SizePx, [int]$maxBot)
$cjkInk = @($inked | Where-Object { -not $_.Ascii })
$ascInk = @($inked | Where-Object { $_.Ascii })
$maxCjkW = if ($cjkInk.Count) { ($cjkInk | ForEach-Object { $_.X2 - $_.X1 + 1 } | Measure-Object -Maximum).Maximum } else { 0 }
$maxAscW = if ($ascInk.Count) { ($ascInk | ForEach-Object { $_.X2 - $_.X1 + 1 } | Measure-Object -Maximum).Maximum } else { 0 }
$blankCjk = @($info | Where-Object { $_.Empty -and -not $_.Ascii }).Count

# ---- 2c. pack the glyphs: tight ink box, bit-continuous rows ----------
$blob     = New-Object System.Collections.Generic.List[byte]
$dscLines = New-Object System.Collections.Generic.List[string]

# glyph id 0 is reserved for "no glyph"
$dscLines.Add('  { .bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0 }, /* id 0 : invalid */')
$glyphId = 1

foreach ($gi in $info) {
  $isAscii = $gi.Ascii
  if ($gi.Empty) {
    $boxW = 0; $boxH = 0; $ofsX = 0; $ofsY = 0
  }
  else {
    $boxW = $gi.X2 - $gi.X1 + 1
    $boxH = $gi.Y2 - $gi.Y1 + 1
    $ofsX = $gi.X1 - $InsetX                  # pen position -> left of the ink
    $ofsY = $lineH - $boxH - $gi.Y1           # keeps the ink on its true row
  }
  $advPx = if ($isAscii) { if ($AsciiAdvPx -gt 0) { $AsciiAdvPx } else { $gi.Adv } } else { $CjkAdvPx }
  $advW  = [int][Math]::Round($advPx * 16)    # 8.4 fixed point (LVGL: (adv_w+8)>>4)

  $bitmapIndex = $blob.Count
  if (-not $gi.Empty) {
    # MSB first, rows follow each other in one continuous bit stream
    $bitAcc = 0; $bitN = 0
    for ($y = 0; $y -lt $boxH; $y++) {
      for ($x = 0; $x -lt $boxW; $x++) {
        $bit = if ($gi.Bmp.GetPixel($gi.X1 + $x, $gi.Y1 + $y).R -gt 127) { 1 } else { 0 }
        $bitAcc = ($bitAcc -shl 1) -bor $bit
        $bitN++
        if ($bitN -eq 8) { $blob.Add([byte]$bitAcc); $bitAcc = 0; $bitN = 0 }
      }
    }
    if ($bitN -gt 0) { $blob.Add([byte]($bitAcc -shl (8 - $bitN))) }   # pad: next glyph stays byte aligned
  }

  if ($isAscii) {
    $dscLines.Add(('  {{ .bitmap_index = {0}, .adv_w = {1}, .box_w = {2}, .box_h = {3}, .ofs_x = {4}, .ofs_y = {5} }}, /* 0x{6:X2} */' -f $bitmapIndex, $advW, $boxW, $boxH, $ofsX, $ofsY, $gi.Cp))
  } else {
    $dscLines.Add(('  {{ .bitmap_index = {0}, .adv_w = {1}, .box_w = {2}, .box_h = {3}, .ofs_x = {4}, .ofs_y = {5} }}, /* U+{6:X4} */' -f $bitmapIndex, $advW, $boxW, $boxH, $ofsX, $ofsY, $gi.Cp))
  }
  $glyphId++
}
foreach ($gi in $info) { if ($gi.Bmp) { $gi.Bmp.Dispose() } }
$font.Dispose()
$bitmapBytes = $blob.Count

$cjkStartId = 1 + $ascii.Count      # first glyph id of the CJK block
$cjkCount   = $cjk.Count
$cmapNum    = if ($cjkCount -gt 0) { 2 } else { 1 }

# ---- 3. emit C --------------------------------------------------------
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// AUTO-GENERATED by tools/gen_lvgl_font.ps1  -  DO NOT EDIT BY HAND')
[void]$sb.AppendLine('//')
[void]$sb.AppendLine(('// {0} glyphs ({1} ASCII + {2} CJK), {3} bytes of bitmap.' -f $info.Count, $ascii.Count, $cjkCount, $bitmapBytes))
[void]$sb.AppendLine('#include "lvgl.h"')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#ifndef LV_FONT_CN_LINE_HEIGHT')
[void]$sb.AppendLine(('#define LV_FONT_CN_LINE_HEIGHT {0}' -f $lineH))
[void]$sb.AppendLine('#endif')
[void]$sb.AppendLine('')

# --- bitmap ---
[void]$sb.AppendLine('static const uint8_t cn_glyph_bitmap[] = {')
for ($i = 0; $i -lt $blob.Count; $i += 16) {
  $chunk = ($blob[$i..([Math]::Min($i + 15, $blob.Count - 1))] | ForEach-Object { $_.ToString() }) -join ','
  [void]$sb.AppendLine('  ' + $chunk + ',')
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')

# --- glyph descriptors ---
[void]$sb.AppendLine('static const lv_font_fmt_txt_glyph_dsc_t cn_glyph_dsc[] = {')
foreach ($l in $dscLines) { [void]$sb.AppendLine($l) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')

# --- CJK unicode list (sparse cmap) ---
if ($cjkCount -gt 0) {
  [void]$sb.AppendLine(('static const uint16_t cn_cjk_list[{0}] = {{' -f $cjkCount))
  for ($i = 0; $i -lt $cjk.Count; $i += 12) {
    $chunk = ($cjk[$i..([Math]::Min($i + 11, $cjk.Count - 1))] | ForEach-Object { '0x{0:X4}' -f $_ }) -join ', '
    [void]$sb.AppendLine('  ' + $chunk + ',')
  }
  [void]$sb.AppendLine('};')
  [void]$sb.AppendLine('')
}

# --- cmaps ---
[void]$sb.AppendLine('static const lv_font_fmt_txt_cmap_t cn_cmaps[] = {')
[void]$sb.AppendLine(('  {{ .range_start = 0x0020, .range_length = {0}, .glyph_id_start = 1, ' -f ($ascii.Count - 1)) +
                     '.unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, ' +
                     '.type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY },')
if ($cjkCount -gt 0) {
  [void]$sb.AppendLine(('  {{ .range_start = 0x0000, .range_length = 0xFFFF, .glyph_id_start = {0}, ' -f $cjkStartId) +
                       '.unicode_list = cn_cjk_list, .glyph_id_ofs_list = NULL, ' +
                       ('.list_length = {0}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY }},' -f $cjkCount))
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')

# --- font descriptor + font object ---
[void]$sb.AppendLine('static lv_font_fmt_txt_glyph_cache_t cn_glyph_cache;')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const lv_font_fmt_txt_dsc_t cn_font_dsc = {')
[void]$sb.AppendLine('  .glyph_bitmap = cn_glyph_bitmap,')
[void]$sb.AppendLine('  .glyph_dsc = cn_glyph_dsc,')
[void]$sb.AppendLine('  .cmaps = cn_cmaps,')
[void]$sb.AppendLine('  .kern_dsc = NULL,')
[void]$sb.AppendLine('  .kern_scale = 0,')
[void]$sb.AppendLine(('  .cmap_num = {0},' -f $cmapNum))
[void]$sb.AppendLine('  .bpp = 1,')
[void]$sb.AppendLine('  .kern_classes = 0,')
[void]$sb.AppendLine('  .bitmap_format = LV_FONT_FMT_TXT_PLAIN,')
[void]$sb.AppendLine('  .cache = &cn_glyph_cache,')
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('const lv_font_t lv_font_cn = {')
[void]$sb.AppendLine('  .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,')
[void]$sb.AppendLine('  .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,')
[void]$sb.AppendLine('  .line_height = LV_FONT_CN_LINE_HEIGHT,')
[void]$sb.AppendLine('  .base_line = 0,')
[void]$sb.AppendLine('  .subpx = LV_FONT_SUBPX_NONE,')
[void]$sb.AppendLine('  .underline_position = -1,')
[void]$sb.AppendLine('  .underline_thickness = 1,')
[void]$sb.AppendLine('  .dsc = &cn_font_dsc,')
[void]$sb.AppendLine('  .fallback = NULL,')
[void]$sb.AppendLine('};')

[System.IO.File]::WriteAllText($outFile, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ''
Write-Host ("font    : {0} {1}px   line_height={2}  inset_x={3}" -f $FontName, $SizePx, $lineH, $InsetX)
Write-Host ("ink     : CJK max {0}px wide, ASCII max {1}px wide, lowest row {2}" -f $maxCjkW, $maxAscW, $maxBot)
Write-Host ("blank   : {0} CJK codepoints had no ink (glyph renders empty)" -f $blankCjk)
if ($maxCjkW -gt $CjkAdvPx) {
  Write-Host ("WARNING : a CJK glyph is {0}px wide but the advance is {1}px -> lower -SizePx" -f $maxCjkW, $CjkAdvPx) -ForegroundColor Yellow
}
Write-Host ("lvgl font: {0} glyphs ({1} ASCII + {2} CJK), {3} bitmap bytes -> {4}" -f $info.Count, $ascii.Count, $cjkCount, $bitmapBytes, $outFile)

