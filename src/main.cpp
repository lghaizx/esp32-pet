// =====================================================================
//  main.cpp  -  ESP32 Pixel Pet : welcome, screens & main loop
//
//  This is the original immediate-mode TFT_eSPI interface. It is only
//  compiled when USE_LVGL == 0 (see config.h); the LVGL rewrite lives in
//  src/app_lvgl.cpp and provides setup()/loop() instead.
// =====================================================================
#include "config.h"

#if !USE_LVGL

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "buzzer.h"
#include "buttons.h"
#include "sensors.h"
#include "eyes.h"
#include "pet.h"
#include "ui.h"
#include "game.h"
#include "cnfont.h"
#include "settings.h"
#include "careaction.h"   // feed / wash / medicine: animation -> stat -> face

#if USE_SD
#include <SPI.h>
#include <SD.h>
#endif

// ---------------------------------------------------------------- globals
TFT_eSPI  tft;
EyeEngine eyes(&tft);
Pet       pet;
CatchGame game;

// Same three beat care actions as the LVGL build (see careaction.h): animation
// first, stat second, expression third - shared code, only the redraw differs.
static CareAction care(pet, eyes);

enum Screen {
  SCR_WELCOME, SCR_FACE, SCR_MENU, SCR_STAT, SCR_SLEEP,
  SCR_EMOTE, SCR_MUSIC, SCR_ABOUT, SCR_GAME
};
Screen screen = SCR_WELCOME;

static const char* MENU[] = {
  "喂食", "玩耍", "睡觉", "洗澡", "吃药",
  "状态", "音乐", "表情", "关于", "重来"
};
static const int MENU_N = (int)(sizeof(MENU) / sizeof(MENU[0]));
int menuSel = 0;

// expression gallery order
static const EyeMood EMOTE_ORDER[] = {
  MOOD_NEUTRAL, MOOD_HAPPY, MOOD_LOVE, MOOD_SURPRISED, MOOD_WINK,
  MOOD_CURIOUS, MOOD_SAD, MOOD_ANGRY, MOOD_SLEEPY, MOOD_DIZZY,
  MOOD_SICK, MOOD_DEAD
};
static const int EMOTE_N = (int)(sizeof(EMOTE_ORDER) / sizeof(EMOTE_ORDER[0]));
int emoteIdx = 0;

// music list
static const uint8_t SONGS[] = { SND_SONG1, SND_SONG2, SND_SONG3 };
static const int SONG_N = (int)(sizeof(SONGS) / sizeof(SONGS[0]));
int songSel = 0;

// timing
uint32_t lastFrame = 0, statusMs = 0, sensorMs = 0, animMs = 0, hudMs = 0;
uint32_t gameResultMs = 0;
uint32_t saveMs = 0;
uint32_t talkMs = 0;
uint32_t flairMs = 0;

// game phase
enum { GPA_INTRO, GPA_PLAY, GPA_RESULT };
int  gamePhase = GPA_INTRO;
bool gameRewarded = false;

static EyeMood lastExpr = MOOD_COUNT;
static bool sdReady = false;

// prototypes
void welcome();
void openMenu();
void menuAction(int sel);
void showStatus();
void showAbout();
void showCantRestart();
void showEmote();
void showMusic();
void startGame();
void showGameResult();
void enterFace();

// ---------------------------------------------------------------- speech
// Short, situational chit-chat shown as a little bubble over the face. Kept
// brief (<= ~7 CJK glyphs) so it always fits inside the bubble.
static void petSpeak() {
  PetState& s = pet.s();
  const char* msg;
  static char idle[80];
  if (s.sick)              msg = (random(2) ? "我不舒服" : "咳咳...");
  else if (s.hunger < 25)  msg = (random(2) ? "我饿了"   : "想吃东西");
  else if (s.energy < 25)  msg = (random(2) ? "好困啊"   : "想睡觉");
  else if (s.clean  < 30)  msg = (random(2) ? "我想洗澡" : "身上脏了");
  else if (s.happy  < 30)  msg = (random(2) ? "陪我玩嘛" : "有点无聊");
  else if (s.happy >= 85)  msg = (random(3) == 0 ? "最喜欢你" : "好开心");
  else {
    settingsPickPhrase(idle, sizeof idle);       // the user enabled catchphrases
    msg = idle;
  }
  eyes.say(msg, 2600);
}

// Light (top left) and temperature (top right) of the scene corners, painted
// into the face sprite by EyeEngine (see drawHud()). Refreshed ~ once a second.
static void updateSensorHud() {
  char l[20], r[20];
  lightHudText(l, sizeof l);
  tempHudText (r, sizeof r);
  eyes.setHud(l, r);
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(60);
  randomSeed(analogRead(LDR_PIN) ^ micros());

  settingsBegin();        // user tunables (talk / expression / growth) from NVS
  buzzerInit();
  buttonsInit();
  sensorsInit();
  updateSensorHud();      // prime the scene corners before the first frame

  tft.init();
  tft.setRotation(SCR_ROTATION);
  tft.fillScreen(COL_BG);

  if (!eyes.begin(&tft)) {
    tft.fillScreen(COL_BAD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_BAD);
    tft.drawString("No RAM for face!", SCR_W / 2, SCR_H / 2, 1);
    while (true) delay(1000);
  }

#if USE_SD
  SPI.begin(18, 19, 23, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, SPI, 8000000);
#endif

  pet.begin();
  // The status table shows the age in one unit only (see petAgeText), so the
  // exact minute count goes here - that is what makes a legacy, 30x inflated
  // save recognisable at a glance.
  Serial.printf("[pet] state: %s, age %u min\n",
                pet.load() ? "inherited from NVS" : "fresh (no save found)",
                (unsigned)pet.s().ageMin);
  {
    char gp[96];
    petGrowthPlanText(gp, sizeof gp);
    Serial.printf("[pet] age: %s\n", gp);
  }

  statusMs = sensorMs = animMs = saveMs = hudMs = millis();
  sfxStartup();
  welcome();
  enterFace();
  lastFrame = millis();
  talkMs  = millis() + random((long)cfg.talkMinMs, (long)cfg.talkMaxMs);
  flairMs = millis() + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
}

void enterFace() {
  lastExpr = MOOD_COUNT;
  screen = SCR_FACE;
  tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG);
  UI::drawStatus(&tft, pet);
  statusMs = millis();
  flairMs = millis() + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
}

// -------------------------------------------------------------- welcome
void welcome() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(FW_NAME, SCR_W / 2, STATUS_Y + 12, 1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("v" FW_VERSION, SCR_W / 2, STATUS_Y + 24, 1);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 40, "A开始", COL_TEXT, COL_BG);

  eyes.setBaseMood(MOOD_SLEEPY);
  uint32_t prev = millis(), t0 = prev;
  int stage = 0;
  bool done = false;
  while (!done) {
    uint32_t now = millis();
    uint32_t dt = now - prev; prev = now;
    eyes.update(dt);
    eyes.render();
    buttonsUpdate();
    buzzerUpdate();

    uint32_t e = now - t0;
    if (stage == 0 && e > 500)  { stage = 1; eyes.setBaseMood(MOOD_NEUTRAL); eyes.blinkNow(); sfxConfirm(); }
    if (stage == 1 && e > 1400) { stage = 2; eyes.lookRandom(); }
    if (stage == 2 && e > 2100) { stage = 3; eyes.setBaseMood(MOOD_HAPPY); eyes.flashMood(MOOD_LOVE, 900); sfxHappy(); }

    if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I) || e > 6000) done = true;
    buttonsEndFrame();
  }
  while (btnDown(BTN_A_I) || btnDown(BTN_B_I)) { buttonsUpdate(); buttonsEndFrame(); delay(5); }
  sfxClick();
}

// ----------------------------------------------------------- menu/actions
void openMenu() { screen = SCR_MENU; UI::drawMenu(&tft, MENU, MENU_N, menuSel); }

void drawEmoteLabel();
void drawMusicLabel();

void menuAction(int sel) {
  switch (sel) {
    // Care actions are three beats long (see careaction.h): the pet plays the
    // animation first, the stat is applied when it ends - so enter the face screen
    // right away, that is where the animation is visible.
    case 0: care.begin(CARE_FEED);     enterFace(); break;
    case 1: startGame();                                                          break;
    case 2: if (!pet.s().dead) { pet.toggleSleep(); sfxSleep();
              if (pet.s().asleep) { screen = SCR_SLEEP; tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG); }
              else enterFace(); }                                                 break;
    case 3: care.begin(CARE_WASH);     enterFace(); break;
    case 4: care.begin(CARE_MEDICINE); enterFace(); break;
    case 5: showStatus();                                                         break;
    case 6: showMusic();                                                          break;
    case 7: showEmote();                                                          break;
    case 8: showAbout();                                                          break;
    case 9:  // restart is only allowed once the pet has died
      if (pet.s().dead) { pet.reset(); lastExpr = MOOD_COUNT; sfxStartup(); enterFace(); }
      else              { sfxCancel(); showCantRestart(); }
      break;
  }
  pet.save();   // persist the effect of every menu action right away
}

void showStatus() {
  PetState& s = pet.s();
  char b[12][40];
  char age[16];
  petAgeText(age, sizeof age, s.ageMin);
  snprintf(b[0], 40, "段 %s", stageName(s.stage));
  snprintf(b[1], 40, "龄 %s", age);          // "42分" / "23时" / "6天"
  snprintf(b[2], 40, "重 %ug", s.weight);
  snprintf(b[3], 40, "饿 %u%%", s.hunger);
  snprintf(b[4], 40, "乐 %u%%", s.happy);
  snprintf(b[5], 40, "力 %u%%", s.energy);
  snprintf(b[6], 40, "康 %u%%", s.health);
  snprintf(b[7], 40, "净 %u%%", s.clean);
  snprintf(b[8], 40, "便 %u", s.poopsTotal);
  snprintf(b[9], 40, "误 %u", s.mistakes);
  char lt[24], lh[24];
  tempText(lt, sizeof lt);
  lightText(lh, sizeof lh);
  snprintf(b[10], 40, "温 %s", lt);
  snprintf(b[11], 40, "光 %s", lh);

  const char* lines[12];
  for (int i = 0; i < 12; i++) lines[i] = b[i];
  UI::drawFullInfo(&tft, "状态", lines, 12);
  screen = SCR_STAT;
}

void showAbout() {
  static const char* lines[] = {
    "ESP32宠物",
    "版本 " FW_VERSION,
    "主控 ESP32",
    "按键 6键",
    "光照36 温度39",
    "A确定 B返回"
  };
  UI::drawFullInfo(&tft, "关于", lines, 6);
  screen = SCR_ABOUT;
}

// Shown when the player picks 「重来」 while the pet is still alive.
void showCantRestart() {
  static const char* lines[] = {
    "宠物还活着",
    "不能重来",
    "",
    "等它去世后",
    "才能重新开始",
    "A/B返回"
  };
  UI::drawFullInfo(&tft, "重来", lines, 6);
  screen = SCR_ABOUT;   // A/B returns to the menu
}

void showEmote() {
  screen = SCR_EMOTE;
  emoteIdx = 0;
  eyes.setBaseMood(EMOTE_ORDER[emoteIdx]);
  drawEmoteLabel();
}

void drawEmoteLabel() {
  tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 10, moodName(EMOTE_ORDER[emoteIdx]), COL_ACCENT, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 34, "左右切换 B返回", COL_DIM, COL_BG);
}

void showMusic() {
  screen = SCR_MUSIC;
  songSel = 0;
  drawMusicLabel();
}

void drawMusicLabel() {
  tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 0,  "音乐", COL_ACCENT, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 16, buzzerName(SONGS[songSel]), COL_TEXT, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 32, buzzerBusy() ? "播放中" : "已停止",
          buzzerBusy() ? COL_GOOD : COL_DIM, COL_BG);
  cnDrawC(&tft, SCR_W / 2, STATUS_Y + 48, "上下选 A播放 B停", COL_DIM, COL_BG);
}

void startGame() {
  game.begin(&tft);
  gameRunIntro(&tft);
  gamePhase = GPA_INTRO;
  gameRewarded = false;
  screen = SCR_GAME;
}

void showGameResult() {
  int sc = game.score();
  int rh = min(30, sc * 2);
  int rf = min(20, sc);
  char b0[32], b1[32], b2[32];
  snprintf(b0, 32, "分数 %d", sc);
  snprintf(b1, 32, "快乐 +%d", rh);
  snprintf(b2, 32, "饥饿 +%d", rf);
  const char* lines[] = { b0, b1, b2, "", "A/B返回" };
  UI::drawFullInfo(&tft, "游戏结束", lines, 5);
}

// ------------------------------------------------------------ frame views
static void frameFace(uint32_t dt) {
  PetState& s = pet.s();
  if (s.asleep && !s.dead) {
    screen = SCR_SLEEP;
    tft.fillRect(0, STATUS_Y, SCR_W, STATUS_H, COL_BG);
    return;
  }
  EyeMood m = pet.expressionMood();
  if (m != lastExpr) { eyes.setBaseMood(m); lastExpr = m; }

  eyes.update(dt);
  eyes.render();

  uint32_t now = millis();
  if (now - statusMs > 700) { statusMs = now; UI::drawStatus(&tft, pet); }

  // Idle small talk, spaced by the user setting (talkMaxMs == 0 switches it off).
  // Muted while a care action plays: the bubble is drawn over the action animation
  // and would talk over beat 3.
  if (!s.dead && cfg.talkMaxMs && !care.busy() && (int32_t)(now - talkMs) >= 0) {
    talkMs = now + random((long)cfg.talkMinMs, (long)cfg.talkMaxMs);
    petSpeak();
  }

  // Ambient expression flair: flash a lively expression now and then so the pet
  // keeps changing even while its stats (and base mood) hardly move.
  if (!s.dead && !s.asleep && !care.busy() && (int32_t)(now - flairMs) >= 0) {
    flairMs = now + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
    const EyeMood base = pet.expressionMood();
    if (base == MOOD_NEUTRAL || base == MOOD_HAPPY ||
        base == MOOD_CURIOUS || base == MOOD_LOVE) {
      static const EyeMood FLAIR[] = { MOOD_HAPPY, MOOD_LOVE, MOOD_SURPRISED,
                                       MOOD_WINK, MOOD_CURIOUS };
      eyes.flashMood(FLAIR[random(sizeof(FLAIR) / sizeof(FLAIR[0]))],
                     700 + random(0, 900));
      if (random(3) == 0) eyes.lookRandom();
    }
  }

  if (btnPressed(BTN_A_I))     { sfxConfirm(); openMenu(); return; }
  if (btnPressed(BTN_B_I))     { eyes.flashMood(MOOD_LOVE, 800); eyes.say("嘿嘿", 1500); pet.petIt(); sfxHappy(); return; }
  if (btnPressed(BTN_UP_I))    { eyes.look(0, -100);    sfxMove(); }
  if (btnPressed(BTN_DOWN_I))  { eyes.look(0,  100);    sfxMove(); }
  if (btnPressed(BTN_LEFT_I))  { eyes.look(-100, 0);    sfxMove(); }
  if (btnPressed(BTN_RIGHT_I)) { eyes.look( 100, 0);    sfxMove(); }
}

static void frameMenu(uint32_t) {
  // full-screen menu: no face rendering, just navigation
  if (btnPressed(BTN_DOWN_I)) {
    menuSel = (menuSel + 1) % MENU_N;
    UI::drawMenu(&tft, MENU, MENU_N, menuSel); sfxMove();
  }
  if (btnPressed(BTN_UP_I)) {
    menuSel = (menuSel + MENU_N - 1) % MENU_N;
    UI::drawMenu(&tft, MENU, MENU_N, menuSel); sfxMove();
  }
  if (btnPressed(BTN_A_I)) { sfxConfirm(); menuAction(menuSel); }
  if (btnPressed(BTN_B_I)) { sfxCancel(); enterFace(); }
}

static void frameStat() {
  if (btnPressed(BTN_B_I) || btnPressed(BTN_A_I)) { sfxCancel(); openMenu(); }
}

static void frameSleep(uint32_t dt) {
  PetState& s = pet.s();
  if (!s.asleep || s.dead) { enterFace(); return; }
  eyes.setBaseMood(MOOD_SLEEPY);
  eyes.update(dt);
  eyes.render();

  uint32_t now = millis();
  if (now - animMs > 500) { animMs = now; UI::drawSleep(&tft, pet); }
  if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I)) { pet.wake(); sfxConfirm(); enterFace(); }
}

static void frameEmote(uint32_t dt) {
  eyes.update(dt);
  eyes.render();
  // LEFT/RIGHT walk the gallery - that is what the "左右切换" hint on the screen
  // promises (the handler used to read UP/DOWN only). UP/DOWN stay as aliases and
  // A keeps stepping forward.
  if (btnPressed(BTN_RIGHT_I) || btnPressed(BTN_DOWN_I) || btnPressed(BTN_A_I)) {
    emoteIdx = (emoteIdx + 1) % EMOTE_N;
    eyes.setBaseMood(EMOTE_ORDER[emoteIdx]);
    drawEmoteLabel(); sfxMove();
  }
  if (btnPressed(BTN_LEFT_I) || btnPressed(BTN_UP_I)) {
    emoteIdx = (emoteIdx + EMOTE_N - 1) % EMOTE_N;
    eyes.setBaseMood(EMOTE_ORDER[emoteIdx]);
    drawEmoteLabel(); sfxMove();
  }
  if (btnPressed(BTN_B_I)) { sfxCancel(); openMenu(); }
}

static void frameMusic(uint32_t) {
  if (btnPressed(BTN_DOWN_I)) { songSel = (songSel + 1) % SONG_N; drawMusicLabel(); sfxMove(); }
  if (btnPressed(BTN_UP_I))   { songSel = (songSel + SONG_N - 1) % SONG_N; drawMusicLabel(); sfxMove(); }
  if (btnPressed(BTN_A_I))    { buzzerPlay(SONGS[songSel], true); drawMusicLabel(); }
  if (btnPressed(BTN_B_I))    { buzzerStop(); sfxCancel(); openMenu(); }
}

static void frameAbout() {
  if (btnPressed(BTN_B_I) || btnPressed(BTN_A_I)) { sfxCancel(); openMenu(); }
}

static void frameGame(uint32_t dt) {
  if (gamePhase == GPA_INTRO) {
    if (btnPressed(BTN_A_I))      { game.start(); gamePhase = GPA_PLAY; sfxConfirm(); }
    else if (btnPressed(BTN_B_I)) { sfxCancel(); openMenu(); }
    return;
  }
  if (gamePhase == GPA_PLAY) {
    game.update(dt);
    game.render();
    if (game.exitRequested()) { sfxCancel(); openMenu(); return; }
    if (game.over()) {
      gamePhase = GPA_RESULT;
      if (!gameRewarded) {
        gameRewarded = true;
        int sc = game.score();
        pet.addHappy(min(30, sc * 2));
        pet.addHunger(min(20, sc));
        eyes.flashMood(MOOD_LOVE, 1500);
      }
      showGameResult();
    }
    return;
  }
  if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I)) { sfxClick(); openMenu(); }
}

// -------------------------------------------------------------- main loop
void loop() {
  uint32_t now = millis();
  buttonsUpdate();
  buzzerUpdate();

  if (now - sensorMs >= 100) {
    sensorMs = now;
    sensorsUpdate();
    if (now - hudMs >= 1000) {
      hudMs = now;
      updateSensorHud();                       // scene corners (light / temp), ~1 Hz
#if BTN_DEBUG
      // Report *meaningful* sensor changes as well, so the ADC wiring on
      // GPIO36 (light) / GPIO39 (temperature) can be checked in the monitor.
      static int dbgLight = -1, dbgTemp = -999;
      const int lp = lightPct(), td = tempDeciC();
      if (abs(lp - dbgLight) >= 2 || abs(td - dbgTemp) >= 20) {
        dbgLight = lp; dbgTemp = td;
        Serial.printf("[sensor] LDR GPIO%d = %d%% (raw %d)   NTC GPIO%d = %d.%dC (raw %d)\n",
                      LDR_PIN, lp, lightRaw(),
                      NTC_PIN, td / 10, abs(td % 10), tempRaw());
      }
#endif
    }
  }

#if BTN_DEBUG
  // report every key-state CHANGE so the wiring can be verified in the monitor
  static uint8_t dbgKeys = 0;
  uint8_t keys = 0;
  for (uint8_t i = 0; i < 6; i++) if (btnDown(i)) keys |= (1 << i);
  if (keys != dbgKeys) {
    dbgKeys = keys;
    Serial.printf("[keys] mask=0x%02X  %s%s%s%s%s%s\n", keys,
                  btnDown(BTN_UP_I)    ? "UP " : "",
                  btnDown(BTN_DOWN_I)  ? "DN " : "",
                  btnDown(BTN_LEFT_I)  ? "LT " : "",
                  btnDown(BTN_RIGHT_I) ? "RT " : "",
                  btnDown(BTN_A_I)     ? "A "  : "",
                  btnDown(BTN_B_I)     ? "B "  : "");
  }
#endif

  // ~30 fps render/handle cadence (keeps button edges low-latency)
  uint32_t dt = now - lastFrame;
  if (dt < 30) return;
  lastFrame = now;
  if (dt > 200) dt = 200;

  pet.update(dt);
  if (pet.consumePoop())   sfxPoop();
  if (pet.consumeEvolve()) { eyes.flashMood(MOOD_LOVE, 1500); sfxEvolve(); pet.save(); }

  // "Cover me": a hand over the light + temperature sensors makes the light fall
  // and the NTC warm up (body heat), so the pet basks in it. The one-shot gesture
  // is consumed every frame (so it can never queue up) but only acted on when the
  // pet is awake on the face screen.
  const bool covered = sensorsCoverEvent();
  if (covered && screen == SCR_FACE && !pet.s().dead && !pet.s().asleep) {
    eyes.flashMood(MOOD_LOVE, 1500);
    eyes.say("好舒服啊", 2200);
    pet.petIt();
    sfxHappy();
#if BTN_DEBUG
    Serial.println("[sensor] hand-cover gesture -> the pet says 好舒服啊");
#endif
  }

  // Care action timeline: the animation runs on its own and the stat lands when it
  // is over (see careaction.h). Driven here rather than in the menu handler
  // because the screen may have changed in between - the action still has to
  // finish, and the status area has to move in the very frame the value did.
  care.update();
  if (care.consumeSettled()) {
    UI::drawStatus(&tft, pet);            // status word + bars, only now
    lastExpr = pet.expressionMood();      // the resting face already moved with the
  }                                       // stat, so do NOT set MOOD_COUNT here: that
                                          // would re-apply the base mood next frame and
                                          // wipe beat 3's flash before it is seen

#if PET_AUTOSAVE_MS
  if (now - saveMs >= PET_AUTOSAVE_MS) { saveMs = now; pet.save(); }   // periodic checkpoint
#endif

  switch (screen) {
    case SCR_FACE:  frameFace(dt);  break;
    case SCR_MENU:  frameMenu(dt);  break;
    case SCR_STAT:  frameStat();    break;
    case SCR_SLEEP: frameSleep(dt); break;
    case SCR_EMOTE: frameEmote(dt); break;
    case SCR_MUSIC: frameMusic(dt); break;
    case SCR_ABOUT: frameAbout();   break;
    case SCR_GAME:  frameGame(dt);  break;
    default: break;
  }

  buttonsEndFrame();
}

#endif  // !USE_LVGL
