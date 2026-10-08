// =====================================================================
//  ui.h  -  screen drawing helpers (status bars / menu / info pages)
// =====================================================================
#pragma once
#include <TFT_eSPI.h>
#include "pet.h"
#include "cnfont.h"

namespace UI {

// bottom status panel (drawn under the face)
void drawStatus(TFT_eSPI* tft, Pet& pet);

// sleep screen panel (z Z z ... + energy + hint)
void drawSleep(TFT_eSPI* tft, Pet& pet);

// scrolling menu in the bottom panel
void drawMenu(TFT_eSPI* tft, const char* const* items, int count, int sel);

// full screen info page (title + lines of text)
void drawFullInfo(TFT_eSPI* tft, const char* title, const char* const* lines, int n);

// small helpers
void drawBar(TFT_eSPI* tft, int x, int y, int w, int h, int val, uint16_t col);
void drawPoop(TFT_eSPI* tft, int x, int y, int s);
void drawHint(TFT_eSPI* tft, const char* txt);
void drawPanel(TFT_eSPI* tft, int x, int y, int w, int h, const char* title);
uint16_t statColor(int v);

} // namespace UI
