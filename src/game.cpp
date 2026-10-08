// =====================================================================
//  game.cpp  -  "Catch the food" mini game
// =====================================================================
#include "game.h"
#include "buzzer.h"
#include "buttons.h"
#include "cnfont.h"
#include <stdio.h>
#include <math.h>

// The playfield covers the *whole* screen. The HUD is not a band of its own: it
// is simply drawn over the top of the field with a transparent background (see
// drawHUD()), which is why falling items spawn below HUD_H instead of at y = 0.
#define HUD_H     14
#define F_TOP     0
#define F_BOT     (SCR_H - 2)
#define BASKET_Y  (SCR_H - 12)
#define BASKET_HW 16
#define BASKET_HH 5
#define ITEM_HALF 7

static const uint16_t F_BG    = RGB565(6, 8, 16);
static const uint16_t F_LINE  = RGB565(38, 58, 92);
static const uint16_t F_STAR  = RGB565(30, 40, 66);
static const uint16_t B_C     = RGB565(70, 150, 235);
static const uint16_t B_D     = RGB565(28, 84, 150);

void CatchGame::begin(TFT_eSPI* tft) { _tft = tft; }

void CatchGame::drawField() {
  _tft->fillRect(0, F_TOP, SCR_W, F_BOT - F_TOP, F_BG);
  _tft->drawRect(0, F_TOP, SCR_W, F_BOT - F_TOP, F_LINE);
  for (int i = 0; i < 26; i++) {
    int x = random(6, SCR_W - 6);
    int y = random(4, F_BOT - 40);        // stars may sit behind the HUD too
    _tft->drawPixel(x, y, F_STAR);
  }
}

// The HUD is *overlaid* on the playfield: the hearts are plain shapes and the
// score is laid down as ink only (no background), so the field keeps showing
// through and redrawing every frame never flickers. The score's own slot is
// wiped first, otherwise a shorter number would leave digits of the previous
// one behind.
void CatchGame::drawHUD() {
  static const int TX = 4;                     // score slot, left edge
  static const int TW = 2 * CN_GLYPH_W + 6 + 4 * 6;   // "分数 " + up to 4 digits
  _tft->fillRect(TX, 0, TW, HUD_H, F_BG);

  char buf[24];
  snprintf(buf, sizeof buf, "分数 %-4d", _score);
  cnDrawOver(_tft, TX, -1, buf, COL_TEXT);

  // hearts
  for (int i = 0; i < 3; i++) {
    int x = SCR_W - 8 - i * 15;
    int y = 7;
    uint16_t c = (i < _lives) ? COL_BAD : RGB565(58, 38, 44);
    _tft->fillCircle(x - 3, y - 2, 3, c);
    _tft->fillCircle(x + 3, y - 2, 3, c);
    _tft->fillTriangle(x - 6, y, x + 6, y, x, y + 6, c);
  }
}

void CatchGame::eraseBasket(int x) {
  _tft->fillRect(x - BASKET_HW - 2, BASKET_Y - BASKET_HH - 2,
                 BASKET_HW * 2 + 4, BASKET_HH * 2 + 4, F_BG);
}

void CatchGame::drawBasket(int x) {
  _tft->fillRoundRect(x - BASKET_HW, BASKET_Y - BASKET_HH,
                      BASKET_HW * 2, BASKET_HH * 2, 4, B_C);
  _tft->drawRoundRect(x - BASKET_HW, BASKET_Y - BASKET_HH,
                      BASKET_HW * 2, BASKET_HH * 2, 4, B_D);
  _tft->fillRect(x - BASKET_HW + 2, BASKET_Y - BASKET_HH + 2,
                 BASKET_HW * 2 - 4, 3, B_D);
}

void CatchGame::drawGood(int x, int y, uint8_t type) {
  if (type == GI_APPLE) {
    _tft->fillCircle(x, y + 1, 6, RGB565(230, 60, 60));
    _tft->fillCircle(x - 2, y - 1, 2, RGB565(255, 150, 150));
    _tft->drawLine(x, y - 5, x + 1, y - 9, RGB565(90, 60, 30));
    _tft->fillEllipse(x + 3, y - 8, 3, 1, RGB565(70, 190, 90));
  } else { // candy
    _tft->fillCircle(x, y, 6, RGB565(255, 120, 200));
    _tft->drawLine(x - 4, y - 3, x + 4, y + 3, 0xFFFF);
    _tft->drawLine(x - 4, y + 3, x + 4, y - 3, 0xFFFF);
    _tft->fillTriangle(x - 6, y - 3, x - 10, y - 6, x - 10, y + 3, RGB565(230, 90, 170));
    _tft->fillTriangle(x + 6, y - 3, x + 10, y - 6, x + 10, y + 3, RGB565(230, 90, 170));
  }
}

void CatchGame::drawRock(int x, int y) {
  uint16_t c = RGB565(130, 135, 150);
  uint16_t d = RGB565(80, 84, 96);
  _tft->fillCircle(x, y, 6, c);
  _tft->fillTriangle(x - 6, y, x + 6, y, x, y + 8, d);
  _tft->fillCircle(x - 2, y - 2, 2, d);
}

void CatchGame::drawItem(const GameItem& it) {
  int x = (int)it.x, y = (int)it.y;
  if (it.type == GI_ROCK) drawRock(x, y);
  else                    drawGood(x, y, it.type);
}

void CatchGame::eraseItem(const GameItem& it) {
  _tft->fillRect(it.px - ITEM_HALF - 6, it.py - ITEM_HALF - 6,
                 (ITEM_HALF + 6) * 2, (ITEM_HALF + 6) * 2, F_BG);
}

// ----------------------------------------------------------------- logic
int CatchGame::fallSpeed() const {
  int s = 80 + _score * 4;
  return s > 320 ? 320 : s;
}
int CatchGame::spawnInterval() const {
  int s = 1200 - _score * 45;
  return s < 430 ? 430 : s;
}

void CatchGame::start() {
  _score = 0; _lives = 3;
  _over = false; _exit = false; _paused = false;
  _spawnAcc = 0; _frame = 0;
  _basketX = SCR_W / 2.0f;
  _bPrevX = -1; _bPrevDrawn = false;
  for (int i = 0; i < GAME_ITEMS; i++) { _it[i].active = false; _it[i].drawn = false; }

  drawField();
  drawHUD();
  drawBasket((int)_basketX);
  _bPrevX = (int)_basketX; _bPrevDrawn = true;
}

void CatchGame::spawn() {
  int slot = -1;
  for (int i = 0; i < GAME_ITEMS; i++) if (!_it[i].active) { slot = i; break; }
  if (slot < 0) return;

  GameItem& it = _it[slot];
  int r = random(100);
  it.type = (r < 72) ? GI_APPLE : (r < 88) ? GI_CANDY : GI_ROCK;
  it.x = (float)random(ITEM_HALF + 4, SCR_W - ITEM_HALF - 4);
  it.y = (float)(HUD_H + ITEM_HALF + 2);   // spawn below the HUD strip
  float v = fallSpeed() * (0.85f + random(0, 31) / 100.0f);
  it.vy = v;
  it.active = true;
  it.drawn = false;
}

void CatchGame::update(uint32_t dt) {
  if (_over) return;

  if (_paused) {
    if (btnPressed(BTN_A_I)) { _paused = false; sfxConfirm(); }
    if (btnPressed(BTN_B_I)) { _exit = true; }
    return;
  }
  if (btnPressed(BTN_B_I)) { _exit = true; return; }
  if (btnPressed(BTN_A_I)) { _paused = true;  sfxClick();   return; }

  float sp = 150.0f * (dt / 1000.0f);
  if (btnDown(BTN_LEFT_I))  _basketX -= sp;
  if (btnDown(BTN_RIGHT_I)) _basketX += sp;
  if (_basketX < BASKET_HW + 3)            _basketX = BASKET_HW + 3;
  if (_basketX > SCR_W - BASKET_HW - 3)    _basketX = SCR_W - BASKET_HW - 3;

  _spawnAcc += dt;
  if (_spawnAcc >= (uint32_t)spawnInterval()) { _spawnAcc = 0; spawn(); }

  for (int i = 0; i < GAME_ITEMS; i++) {
    GameItem& it = _it[i];
    if (!it.active) continue;
    it.y += it.vy * (dt / 1000.0f);

    bool hitBasket = (it.y + ITEM_HALF >= BASKET_Y - BASKET_HH) &&
                     (it.y - ITEM_HALF <= BASKET_Y + BASKET_HH);
    bool overBasket = fabsf(it.x - _basketX) <= (BASKET_HW + ITEM_HALF - 4);

    if (hitBasket && overBasket) {
      if (it.type == GI_ROCK) { if (_lives > 0) _lives--; sfxError(); }
      else                    { _score++; sfxEat(); }
      it.active = false;
      continue;
    }
    if (it.y > F_BOT + ITEM_HALF) it.active = false;   // fell off the bottom
  }

  if (_lives <= 0) { _over = true; sfxSad(); }
}

void CatchGame::render() {
  if (!_tft) return;
  _frame++;

  int bx = (int)_basketX;
  if (!_bPrevDrawn) { drawBasket(bx); _bPrevX = bx; _bPrevDrawn = true; }
  else if (_bPrevX != bx) { eraseBasket(_bPrevX); drawBasket(bx); _bPrevX = bx; }

  if (!_paused) {
    for (int i = 0; i < GAME_ITEMS; i++) {
      GameItem& it = _it[i];
      if (it.drawn) { eraseItem(it); it.drawn = false; }
      if (it.active) {
        drawItem(it);
        it.px = (int)it.x; it.py = (int)it.y; it.drawn = true;
      }
    }
  }

  drawHUD();

  if (_paused) {
    int bw = 120, bh = 46;
    int bx = (SCR_W - bw) / 2, by = (SCR_H - bh) / 2;
    _tft->fillRoundRect(bx, by, bw, bh, 6, COL_PANEL);
    _tft->drawRoundRect(bx, by, bw, bh, 6, COL_PANEL2);
    cnDrawC(_tft, SCR_W / 2, by + 4,  "暂停",        COL_TEXT, COL_PANEL);
    cnDrawC(_tft, SCR_W / 2, by + 24, "A继续 B退出", COL_DIM,  COL_PANEL);
  }
}

// ------------------------------------------------------------- splash
void gameRunIntro(TFT_eSPI* tft) {
  tft->fillScreen(COL_BG);
  // double rounded frame so the splash does not look like a bare text page
  tft->drawRoundRect(2, 2, SCR_W - 4, SCR_H - 4, 5, RGB565(56, 70, 108));
  tft->drawRoundRect(3, 3, SCR_W - 6, SCR_H - 6, 5, RGB565(30, 37, 60));

  cnDrawC(tft, SCR_W / 2, 12,        "接食物",     COL_ACCENT, COL_BG);
  cnDrawC(tft, SCR_W / 2, 34,        "左右移动篮子", COL_TEXT,   COL_BG);
  cnDrawC(tft, SCR_W / 2, 50,        "A暂停 B退出", COL_DIM,    COL_BG);
  tft->drawFastHLine(28, 70, SCR_W - 56, RGB565(30, 37, 60));
  cnDrawC(tft, SCR_W / 2, 76,        "接住水果和糖", COL_GOOD,   COL_BG);
  cnDrawC(tft, SCR_W / 2, 94,        "避开石头",   COL_BAD,    COL_BG);
  cnDrawC(tft, SCR_W / 2, SCR_H - 18, "A开始",     COL_DIM,    COL_BG);
}
