// =====================================================================
//  game.h  -  "Catch the food" mini game
// =====================================================================
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

#define GAME_ITEMS 6

enum GameItemType : uint8_t { GI_APPLE = 0, GI_CANDY, GI_ROCK };

struct GameItem {
  bool   active;
  uint8_t type;
  float  x, y, vy;
  int16_t px, py;          // previous draw centre (for erase)
  bool   drawn;
};

class CatchGame {
public:
  void begin(TFT_eSPI* tft);
  void start();
  void update(uint32_t dt);   // reads buttons, advances world
  void render();              // draws the current frame

  bool over() const          { return _over; }
  bool exitRequested() const { return _exit; }
  int  score() const         { return _score; }
  int  lives() const         { return _lives; }

private:
  TFT_eSPI* _tft = nullptr;
  GameItem  _it[GAME_ITEMS];
  float     _basketX = 0;
  int16_t   _bPrevX  = -1;
  bool      _bPrevDrawn = false;
  int       _score = 0, _lives = 3;
  bool      _over = false, _exit = false, _paused = false;
  uint32_t  _spawnAcc = 0, _frame = 0;

  void     drawField();
  void     drawBasket(int x);
  void     eraseBasket(int x);
  void     drawItem(const GameItem& it);
  void     eraseItem(const GameItem& it);
  void     drawGood(int x, int y, uint8_t type);
  void     drawRock(int x, int y);
  void     drawHUD();
  void     spawn();
  int      fallSpeed() const;
  int      spawnInterval() const;
};

void gameRunIntro(TFT_eSPI* tft);   // "GET READY" splash
