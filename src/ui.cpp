// =====================================================================
//  ui.cpp  -  screen drawing helpers
// =====================================================================
#include "ui.h"
#include "config.h"
#include "cnfont.h"
#include <stdio.h>

namespace UI {

namespace {
// One off-screen buffer for the whole bottom status strip. Drawing the panel
// into it and then pushing it in a single burst removes the visible "flash"
// that a plain fillRect + redraw produced on every update (~700 ms).
TFT_eSprite* panelSprite(TFT_eSPI* tft) {
  static TFT_eSprite spr(tft);
  static int state = 0;                       // 0 = not tried, 1 = ok, -1 = failed
  if (state == 0) {
    spr.setColorDepth(16);
    spr.setSwapBytes(true);
    state = (spr.createSprite(SCR_W, STATUS_H) != nullptr) ? 1 : -1;
  }
  return (state == 1) ? &spr : nullptr;
}
} // namespace

uint16_t statColor(int v) {
  if (v > 60) return COL_GOOD;
  if (v > 30) return COL_WARN;
  return COL_BAD;
}

void drawBar(TFT_eSPI* tft, int x, int y, int w, int h, int val, uint16_t col) {
  if (val < 0) val = 0; if (val > 100) val = 100;
  tft->drawRoundRect(x, y, w, h, 3, COL_PANEL2);
  int fw = (w - 2) * val / 100;
  if (fw > 0) tft->fillRoundRect(x + 1, y + 1, fw, h - 2, 2, col);
}

void drawPoop(TFT_eSPI* tft, int x, int y, int s) {
  uint16_t c = RGB565(120, 78, 40);
  uint16_t d = RGB565(80, 50, 24);
  tft->fillTriangle(x - s, y, x + s, y, x, y - (int)(s * 1.3), c);
  tft->fillRect(x - s, y, s * 2, s / 2, d);
  tft->fillCircle(x - s / 2, y - s / 3, s / 2, c);
}

void drawHint(TFT_eSPI* tft, const char* txt) {
  cnDrawC(tft, SCR_W / 2, SCR_H - CN_GLYPH_H - 3, txt, COL_DIM, COL_BG);
}

void drawPanel(TFT_eSPI* tft, int x, int y, int w, int h, const char* title) {
  tft->fillRoundRect(x, y, w, h, 6, COL_PANEL);
  tft->drawRoundRect(x, y, w, h, 6, COL_PANEL2);
  if (title) cnDraw(tft, x + 8, y + 4, title, COL_ACCENT, COL_PANEL);
}

// ------------------------------------------------------------- status
void drawStatus(TFT_eSPI* tft, Pet& pet) {
  PetState& s = pet.s();
  TFT_eSprite* spr = panelSprite(tft);
  TFT_eSPI* d = spr ? (TFT_eSPI*)spr : tft;
  const int oy = spr ? 0 : STATUS_Y;          // panel-local vs absolute Y

  d->fillRect(0, oy, SCR_W, STATUS_H, COL_BG);

  const char* status =
      s.dead          ? "死亡" :
      s.asleep        ? "睡觉" :
      s.sick          ? "生病" :
      (s.hunger < 20) ? "饥饿" :
      (s.clean  < 25) ? "脏了" : "很棒";

  // header: stage (left) + status word (right)
  cnDraw (d, 4,         oy + 1, stageName(s.stage), COL_TEXT, COL_BG);
  cnDrawR(d, SCR_W - 4, oy + 1, status,
          s.dead ? COL_BAD : s.sick ? COL_SICK : COL_DIM, COL_BG);

  // five compact stat bars: two columns of (up to) three rows
  struct Row { const char* l; int v; };
  Row rows[5] = {
    { "饥饿", s.hunger }, { "快乐", s.happy }, { "体力", s.energy },
    { "健康", s.health }, { "清洁", s.clean }
  };
  int barTop = oy + CN_GLYPH_H + 2;
  int rowH   = (oy + STATUS_H - barTop) / 3;
  int colW   = SCR_W / 2;
  for (int i = 0; i < 5; i++) {
    int c = i & 1, r = i >> 1;
    int x = c * colW + 4;
    int y = barTop + r * rowH;
    cnDraw(d, x, y, rows[i].l, COL_DIM, COL_BG);
    drawBar(d, x + 2 * CN_GLYPH_W + 2, y + 4, colW - 2 * CN_GLYPH_W - 10, 8,
            rows[i].v, statColor(rows[i].v));
  }

  if (spr) spr->pushSprite(0, STATUS_Y);
}

// -------------------------------------------------------------- sleep panel
void drawSleep(TFT_eSPI* tft, Pet& pet) {
  TFT_eSprite* spr = panelSprite(tft);
  TFT_eSPI* d = spr ? (TFT_eSPI*)spr : tft;
  const int oy = spr ? 0 : STATUS_Y;

  d->fillRect(0, oy, SCR_W, STATUS_H, COL_BG);
  cnDrawC(d, SCR_W / 2, oy + 2,  "z z z ...", COL_ACCENT, COL_BG);
  char b[24];
  snprintf(b, sizeof b, "体力 %u%%", pet.s().energy);
  cnDrawC(d, SCR_W / 2, oy + 22, b, COL_DIM, COL_BG);
  cnDrawC(d, SCR_W / 2, oy + 44, "A起床 B返回", COL_DIM, COL_BG);

  if (spr) spr->pushSprite(0, STATUS_Y);
}

// --------------------------------------------------------------- menu
void drawMenu(TFT_eSPI* tft, const char* const* items, int count, int sel) {
  tft->fillScreen(COL_BG);
  tft->fillRect(0, 0, SCR_W, 18, COL_PANEL);
  cnDraw (tft, 4,         2, "菜单",   COL_ACCENT, COL_PANEL);
  cnDrawR(tft, SCR_W - 4, 2, "B:返回", COL_DIM,    COL_PANEL);

  const int rowh   = 17;
  const int startY = 22;
  int vis = (SCR_H - startY) / rowh;
  if (vis < 1) vis = 1;

  int top = sel - vis / 2;
  if (top < 0) top = 0;
  if (top > count - vis) top = count - vis;
  if (top < 0) top = 0;

  for (int r = 0; r < vis && (top + r) < count; r++) {
    int i = top + r;
    int y = startY + r * rowh;
    bool s = (i == sel);
    if (s) tft->fillRoundRect(2, y, SCR_W - 4, rowh - 1, 3, COL_PANEL2);
    cnDraw(tft, 16, y + 1, items[i], s ? COL_ACCENT : COL_TEXT, s ? COL_PANEL2 : COL_BG);
    if (s) tft->fillTriangle(4, y + 3, 4, y + rowh - 4, 10, y + (rowh - 1) / 2, COL_ACCENT);
  }

  if (top > 0)           tft->fillTriangle(SCR_W - 8, 20, SCR_W - 2, 20, SCR_W - 5, 24, COL_DIM);
  if (top + vis < count) tft->fillTriangle(SCR_W - 8, SCR_H - 2, SCR_W - 2, SCR_H - 2, SCR_W - 5, SCR_H - 6, COL_DIM);
}

// --------------------------------------------------------- info page
void drawFullInfo(TFT_eSPI* tft, const char* title, const char* const* lines, int n) {
  tft->fillScreen(COL_BG);
  tft->fillRect(0, 0, SCR_W, 18, COL_PANEL);
  cnDraw (tft, 4,         2, title,   COL_ACCENT, COL_PANEL);
  cnDrawR(tft, SCR_W - 4, 2, "B:返回", COL_DIM,    COL_PANEL);

  const int startY = 22;
  // switch to 2 columns only when one column can no longer keep 16px spacing
  int cols = 1, rows = n;
  while (cols < 2 && rows > (SCR_H - startY - 2) / CN_GLYPH_H) {
    cols++;
    rows = (n + cols - 1) / cols;
  }
  int lineH = (SCR_H - startY - 2) / rows;
  if (lineH > CN_GLYPH_H + 8) lineH = CN_GLYPH_H + 8;
  int colW = SCR_W / cols;

  for (int i = 0; i < n; i++) {
    int c = i / rows, r = i % rows;
    int x = c * colW + 4;
    int y = startY + r * lineH;
    cnDraw(tft, x, y, lines[i], COL_TEXT, COL_BG);
  }
}

} // namespace UI
