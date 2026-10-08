// =====================================================================
//  cnfont.cpp  -  16x16 CJK bitmap renderer (data in cnfont_data.cpp)
// =====================================================================
#include "cnfont.h"

// ------------------------------------------------------------ UTF-8
static uint16_t utf8Next(const char** s) {
  const uint8_t* p = (const uint8_t*)*s;
  uint16_t c = *p++;
  if (c < 0x80) { *s = (const char*)p; return c; }
  if ((c & 0xE0) == 0xC0) {
    c = (uint16_t)((c & 0x1F) << 6) | (uint16_t)(*p++ & 0x3F);
  } else if ((c & 0xF0) == 0xE0) {
    c = (uint16_t)((c & 0x0F) << 12) | (uint16_t)((*p++ & 0x3F) << 6);
    c |= (uint16_t)(*p++ & 0x3F);
  } else {
    c = '?'; p += 2;                 // 4-byte (out of BMP) -> placeholder
  }
  *s = (const char*)p;
  return c;
}

static int cnFind(uint16_t code) {
  int lo = 0, hi = (int)CN_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint16_t c = CN_TABLE[mid].code;
    if (c == code) return CN_TABLE[mid].idx;
    if (c < code) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

bool cnHasGlyph(uint16_t code) { return cnFind(code) >= 0; }

// ------------------------------------------------------------ drawing
static void drawCJKGlyph(TFT_eSPI* tft, int x, int y, int idx, uint16_t fg, uint16_t bg) {
  const uint8_t* g = CN_BLOB + (size_t)idx * 32;
  tft->fillRect(x, y, CN_GLYPH_W, CN_GLYPH_H, bg);
  for (int row = 0; row < CN_GLYPH_H; row++) {
    uint16_t bits = (uint16_t)((g[row * 2] << 8) | g[row * 2 + 1]);
    if (!bits) continue;
    for (int col = 0; col < CN_GLYPH_W; col++)
      if (bits & (0x8000 >> col)) tft->drawPixel(x + col, y + row, fg);
  }
}

int cnDraw(TFT_eSPI* tft, int x, int y, const char* s, uint16_t fg, uint16_t bg) {
  int cx = x;
  while (*s) {
    uint16_t code = utf8Next(&s);
    if (code == 0) break;
    if (code < 0x20) continue;                 // control chars
    if (code < 0x80) {                          // half-width ASCII (GLCD 6x8)
      tft->drawChar(cx, y + (CN_GLYPH_H - 8), (uint8_t)code, fg, bg, 1);
      cx += 6;
    } else {
      int idx = cnFind(code);
      if (idx >= 0) drawCJKGlyph(tft, cx, y, idx, fg, bg);
      else          tft->fillRect(cx, y, CN_GLYPH_W, CN_GLYPH_H, bg);   // missing -> blank
      cx += CN_GLYPH_W;
    }
  }
  return cx;
}

int cnStrW(const char* s) {
  int w = 0;
  while (*s) {
    uint16_t code = utf8Next(&s);
    if (code < 0x20) continue;
    w += (code < 0x80) ? 6 : CN_GLYPH_W;
  }
  return w;
}

void cnDrawC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg, uint16_t bg) {
  cnDraw(tft, cx - cnStrW(s) / 2, y, s, fg, bg);
}

void cnDrawR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg, uint16_t bg) {
  cnDraw(tft, rx - cnStrW(s), y, s, fg, bg);
}

// ------------------------------------------------- "over" (transparent ink)
// Plot the set pixels of one CJK glyph, leaving every other pixel untouched.
static void plotCJKGlyph(TFT_eSPI* tft, int x, int y, int idx, uint16_t fg) {
  const uint8_t* g = CN_BLOB + (size_t)idx * 32;
  for (int row = 0; row < CN_GLYPH_H; row++) {
    uint16_t bits = (uint16_t)((g[row * 2] << 8) | g[row * 2 + 1]);
    if (!bits) continue;
    for (int col = 0; col < CN_GLYPH_W; col++)
      if (bits & (0x8000 >> col)) tft->drawPixel(x + col, y + row, fg);
  }
}

int cnDrawOver(TFT_eSPI* tft, int x, int y, const char* s, uint16_t fg) {
  int cx = x;
  while (*s) {
    uint16_t code = utf8Next(&s);
    if (code == 0) break;
    if (code < 0x20) continue;
    if (code < 0x80) {
      // TFT_eSPI renders with no background when fg == bg (drawChar then only
      // plots the set pixels of the built in 5x8 font).
      tft->drawChar(cx, y + (CN_GLYPH_H - 8), (uint8_t)code, fg, fg, 1);
      cx += 6;
    } else {
      int idx = cnFind(code);
      if (idx >= 0) plotCJKGlyph(tft, cx, y, idx, fg);   // missing glyph: nothing
      cx += CN_GLYPH_W;
    }
  }
  return cx;
}

void cnDrawOverC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg) {
  cnDrawOver(tft, cx - cnStrW(s) / 2, y, s, fg);
}

void cnDrawOverR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg) {
  cnDrawOver(tft, rx - cnStrW(s), y, s, fg);
}

// ------------------------------------------------- \"over\" + shrunk CJK glyphs
// Plot the set pixels of one CJK glyph scaled down to px*px, box-max sampled:
// an output pixel lights up when *any* source pixel of its source box is set,
// which keeps the thin (1 px) strokes of the 16x16 bitmap from disappearing.
// The result is bottom aligned inside the normal 16 px cell so it shares the
// baseline with the ASCII digits next to it.
static void plotCJKGlyphSmall(TFT_eSPI* tft, int x, int y, int idx, uint16_t fg, int px) {
  if (px < 4) px = 4; if (px > CN_GLYPH_W) px = CN_GLYPH_W;
  const uint8_t* g = CN_BLOB + (size_t)idx * 32;
  const int y0 = y + (CN_GLYPH_H - px);          // bottom aligned in the cell
  for (int oy = 0; oy < px; oy++) {
    int sy0 = oy * CN_GLYPH_H / px;
    int sy1 = (oy + 1) * CN_GLYPH_H / px - 1;
    if (sy1 < sy0) sy1 = sy0;
    for (int ox = 0; ox < px; ox++) {
      int sx0 = ox * CN_GLYPH_W / px;
      int sx1 = (ox + 1) * CN_GLYPH_W / px - 1;
      if (sx1 < sx0) sx1 = sx0;
      bool on = false;
      for (int sy = sy0; sy <= sy1 && !on; sy++) {
        uint16_t bits = (uint16_t)((g[sy * 2] << 8) | g[sy * 2 + 1]);
        if (!bits) continue;
        for (int sx = sx0; sx <= sx1; sx++)
          if (bits & (0x8000 >> sx)) { on = true; break; }
      }
      if (on) tft->drawPixel(x + ox, y0 + oy, fg);
    }
  }
}

// pixel width of a UTF-8 string when CJK glyphs are shrunk to px
static int cnStrWSmall(const char* s, int px) {
  int w = 0;
  while (*s) {
    uint16_t code = utf8Next(&s);
    if (code < 0x20) continue;
    w += (code < 0x80) ? 6 : px;
  }
  return w;
}

int cnDrawOverSm(TFT_eSPI* tft, int x, int y, const char* s, uint16_t fg, int px) {
  int cx = x;
  while (*s) {
    uint16_t code = utf8Next(&s);
    if (code == 0) break;
    if (code < 0x20) continue;
    if (code < 0x80) {
      tft->drawChar(cx, y + (CN_GLYPH_H - 8), (uint8_t)code, fg, fg, 1);
      cx += 6;
    } else {
      int idx = cnFind(code);
      if (idx >= 0) plotCJKGlyphSmall(tft, cx, y, idx, fg, px);
      cx += px;
    }
  }
  return cx;
}

void cnDrawOverSmC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg, int px) {
  cnDrawOverSm(tft, cx - cnStrWSmall(s, px) / 2, y, s, fg, px);
}

void cnDrawOverSmR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg, int px) {
  cnDrawOverSm(tft, rx - cnStrWSmall(s, px), y, s, fg, px);
}
