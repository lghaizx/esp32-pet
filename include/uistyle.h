// =====================================================================
//  uistyle.h  -  the design tokens of the LVGL interface
//
//  Every screen of the pet is assembled from these numbers, so the whole
//  interface shares one visual language:
//
//    backdrop     #080A14 (COL_BG) - a dark blue-black, never pure black
//    surfaces     cards / chips: 5 px radius, vertical gradient
//                 #20273F -> #111626, hairline edge #38466C
//    accent       cyan #00C8FF (text, titles) and #60CDF5 (the focused row);
//                 green / amber / red (COL_GOOD/WARN/BAD) are *state* colours
//                 and only ever appear on the stat bars and status words
//    radius       5 px for cards and rows, 8 px for pills (a chip's height/2)
//    rhythm       2 px inside a row, 4 px between rows, 4 px page margin
//    type         16 px CJK cell (lv_font_cn) for words, 14 px Montserrat for
//                 digits + latin (it has the same 16 px line height as the CJK
//                 cell, so a mixed row never looks crooked), and 20 px
//                 Montserrat for the one "hero" number of a page
//    icons        LV_SYMBOL_* (Font Awesome, already inside LVGL's Montserrat
//                 fonts) - the interface therefore needs no image assets
//
//  The legacy TFT_eSPI UI (src/ui.cpp) keeps its own palette from config.h;
//  this file only dresses the LVGL screens built in src/app_lvgl.cpp.
// =====================================================================
#pragma once

#include "config.h"       // RGB565(), COL_BG / COL_TEXT / COL_DIM / COL_...
#include "lvgl.h"
#include "lv_font_cn.h"   // lv_font_cn : 16 px CJK + half width ASCII

/* ---------------------------------------------------------------- surfaces */
static const uint16_t UI_CARD1 = RGB565(32, 39, 63);    // card gradient: top
static const uint16_t UI_CARD2 = RGB565(17, 22, 38);    // card gradient: bottom
static const uint16_t UI_EDGE  = RGB565(56, 70, 108);   // card edge / hairline
static const uint16_t UI_SEP   = RGB565(30, 36, 58);    // table row separator
static const uint16_t UI_ACC   = RGB565(96, 205, 245);  // secondary (cyan) accent
static const uint16_t UI_HDR1  = RGB565(31, 39, 63);    // header band: top
static const uint16_t UI_HDR2  = RGB565(18, 23, 40);    // header band: bottom
static const uint16_t UI_TRACK = RGB565(28, 34, 56);    // bar groove
static const uint16_t UI_DOT   = RGB565(50, 61, 92);    // inactive carousel dot

/* ---------------------------------------------------------------- geometry */
#define UI_HDR_H  18      // header band (title + "B:返回")
#define UI_CHIP_H 17      // hint "key cap" height
#define UI_ROW_H  17      // menu / table row pitch
#define UI_INFO_Y 22      // first content row
#define UI_R_CARD 5       // cards and the focused menu row
#define UI_R_PILL 8       // pills (= half of a chip's height)
#define UI_PAD    4       // page margin
#define UI_GAP    2       // inside a row

/* --------------------------------------------------------------- type scale */
// Three sizes, no more: the CJK cell for words, one Latin size for every digit
// and one "hero" size for the single big number of a page. A fourth size is
// tempting but each Montserrat size costs ~16 KB of flash (~13 % of this app's
// budget), so the scale stays at three until a screen really needs one more.
#define UI_TXT_CJK  (&lv_font_cn)             // words (16 px CJK cell)
#define UI_TXT_SM   (&lv_font_montserrat_14)  // digits / latin inside a row
#define UI_TXT_HERO (&lv_font_montserrat_20)  // the one big number of a page

/* ---------------------------------------------------------------- helpers */
// Scale a packed RGB565 colour: k < 8 darkens it, k > 8 brightens it.
static inline uint16_t shade(uint16_t c, int k) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (r * k) >> 3; g = (g * k) >> 3; b = (b * k) >> 3;
  if (r > 0x1F) r = 0x1F;
  if (g > 0x3F) g = 0x3F;
  if (b > 0x1F) b = 0x1F;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Pick the font for a string: pure ASCII (i.e. every value of a data page:
// "45%", "12m", "1.0.0") is drawn with Montserrat, which is sharper and never
// 8 px wide, while anything carrying CJK stays in the 16 px CJK font. This is
// what lets a data row read as "dim CJK label + bright right aligned number".
static inline const lv_font_t* uiFont(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; p && *p; p++)
    if (*p >= 0x80) return UI_TXT_CJK;
  return UI_TXT_SM;
}

/* ---------------------------------------------------------- icon + word font */
// A label can carry exactly one font, and the 16 px CJK cell has no glyph for
// LV_SYMBOL_* (Font Awesome lives inside Montserrat). LVGL's own answer is a
// *fallback* chain, so this is one RAM copy (~60 B) of the CJK font that points
// at Montserrat for whatever it does not have. Any CJK label can then be written
// as LV_SYMBOL_BELL " 喂食" - icon and word in a single widget, with no second
// font, no image asset and (unlike a second label per row) no LVGL pool cost.
static inline const lv_font_t* uiFontSymbols() {
  static lv_font_t f;                       // copy of lv_font_cn, in RAM
  static bool ready = false;
  if (!ready) { f = lv_font_cn; f.fallback = UI_TXT_SM; ready = true; }
  return &f;
}
