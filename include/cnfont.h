// =====================================================================
//  cnfont.h  -  embedded 16x16 CJK bitmap font + UTF-8 text helpers
//  Glyph data lives in cnfont_data.cpp, auto-generated at build time by
//  tools/gen_cnfont.ps1 (it scans the sources for non-ASCII characters).
// =====================================================================
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

#define CN_GLYPH_W 16
#define CN_GLYPH_H 16

// one glyph = 16 rows * 2 bytes (MSB = leftmost pixel)
struct CNGlyph {
  uint16_t code;   // Unicode codepoint
  uint16_t idx;    // CN_BLOB index = idx * 32
};

extern const uint16_t CN_COUNT;
extern const CNGlyph  CN_TABLE[];   // sorted by code (binary search)
extern const uint8_t  CN_BLOB[];

// true when the codepoint has a glyph
bool cnHasGlyph(uint16_t code);

// pixel width of a UTF-8 string (CJK = 16, ASCII = 6)
int  cnStrW(const char* s);

// draw UTF-8 text. y = TOP of the 16px CJK cell; ASCII is baseline aligned.
// returns the x position after the last glyph.
int  cnDraw (TFT_eSPI* tft, int x,  int y, const char* s, uint16_t fg, uint16_t bg);
void cnDrawC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg, uint16_t bg); // centred
void cnDrawR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg, uint16_t bg); // right aligned

// Same three, but with a *transparent* background: only the glyph ink is
// plotted and nothing is erased. Use them to lay text over an existing picture
// (the game HUD is drawn that way); whoever owns the pixels behind the text
// keeps showing through.
int  cnDrawOver (TFT_eSPI* tft, int x,  int y, const char* s, uint16_t fg);
void cnDrawOverC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg);
void cnDrawOverR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg);

// Same three plotters, but every CJK glyph is *shrunk* to `px` pixels (box-max
// downscale, so the 1 px strokes of a 16 px glyph survive) and bottom aligned
// inside the normal 16 px cell; ASCII keeps its 6x8 size on that same baseline.
// Used for the small sensor read-out in the scene corners, where the full 16 px
// cell is too loud next to the tiny digits.
int  cnDrawOverSm (TFT_eSPI* tft, int x,  int y, const char* s, uint16_t fg, int px);
void cnDrawOverSmC(TFT_eSPI* tft, int cx, int y, const char* s, uint16_t fg, int px);
void cnDrawOverSmR(TFT_eSPI* tft, int rx, int y, const char* s, uint16_t fg, int px);
