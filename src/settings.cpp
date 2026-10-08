// =====================================================================
//  settings.cpp  -  persisted user tunables (see settings.h)
// =====================================================================
#include "settings.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// Built-in catchphrase candidates. Kept short (<= 8 CJK glyphs) so they always
// fit inside the speech bubble. They are string literals on purpose: the font
// generators scan the sources and embed a glyph for every character they find,
// which is what makes these selectable-sentence phrases possible at all.
static const char* const PHRASES[PHRASE_MAX] = {
  "你好呀",     "在忙什么",   "摸摸我",     "今天天气不错",
  "陪我玩吧",   "我有点饿",   "想睡觉了",   "给我洗澡吧",
  "好开心",     "最喜欢你",   "抱抱我",     "你在干嘛",
  "我要出去玩", "有点无聊",   "唱首歌吧",   "我长大了",
  "别不理我",   "好舒服呀",   "我都饿扁了", "一起玩吧",
  "今天也要开心", "我会乖乖的", "早点休息",  "晚安啦"
};

PetCfg cfg;
static Preferences s_prefs;

static inline uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void settingsDefaults() {
  cfg.talkMinMs = PET_TALK_MIN_MS ? PET_TALK_MIN_MS : 18000UL;
  cfg.talkMaxMs = PET_TALK_MAX_MS ? PET_TALK_MAX_MS : 42000UL;
  cfg.exprMinMs = 6000UL;      // expression flair 6 .. 15 s
  cfg.exprMaxMs = 15000UL;
  cfg.growthPct = 100;         // normal growth speed
  for (int i = 0; i < PHRASE_MAX; i++) cfg.phraseOn[i] = (i < 4) ? 1 : 0;
}

void settingsBegin() {
  settingsDefaults();
  // read-write so the namespace is created on the very first boot (a read-only
  // open of a missing namespace would log an NVS NOT_FOUND error to Serial)
  s_prefs.begin("cfg", false);
  cfg.talkMinMs = s_prefs.getUInt("talkMin", cfg.talkMinMs);
  cfg.talkMaxMs = s_prefs.getUInt("talkMax", cfg.talkMaxMs);
  cfg.exprMinMs = s_prefs.getUInt("exprMin", cfg.exprMinMs);
  cfg.exprMaxMs = s_prefs.getUInt("exprMax", cfg.exprMaxMs);
  cfg.growthPct = (uint16_t)s_prefs.getUShort("growth", cfg.growthPct);
  // guard: getBytes() on a missing key logs an NVS error, so only read when set
  if (s_prefs.isKey("phOn")) s_prefs.getBytes("phOn", cfg.phraseOn, sizeof(cfg.phraseOn));
  s_prefs.end();

  // sanitise whatever came back (a corrupt blob must not brick the pet)
  cfg.talkMinMs = clampU32(cfg.talkMinMs, 2000UL, 600000UL);
  cfg.talkMaxMs = clampU32(cfg.talkMaxMs, cfg.talkMinMs, 600000UL);
  cfg.exprMinMs = clampU32(cfg.exprMinMs, 1000UL, 600000UL);
  cfg.exprMaxMs = clampU32(cfg.exprMaxMs, cfg.exprMinMs, 600000UL);
  if (cfg.growthPct < 20)  cfg.growthPct = 20;
  if (cfg.growthPct > 400) cfg.growthPct = 400;
}

void settingsSave() {
  s_prefs.begin("cfg", false);
  s_prefs.putUInt("talkMin", cfg.talkMinMs);
  s_prefs.putUInt("talkMax", cfg.talkMaxMs);
  s_prefs.putUInt("exprMin", cfg.exprMinMs);
  s_prefs.putUInt("exprMax", cfg.exprMaxMs);
  s_prefs.putUShort("growth", cfg.growthPct);
  s_prefs.putBytes("phOn", cfg.phraseOn, sizeof(cfg.phraseOn));
  s_prefs.end();
}

int         settingsPhraseCount() { return PHRASE_MAX; }
const char* settingsPhrase(int i) {
  return (i >= 0 && i < PHRASE_MAX) ? PHRASES[i] : "";
}
bool settingsPhraseEnabled(int i) {
  return (i >= 0 && i < PHRASE_MAX) && cfg.phraseOn[i];
}
void settingsSetPhraseEnabled(int i, bool on) {
  if (i >= 0 && i < PHRASE_MAX) cfg.phraseOn[i] = on ? 1 : 0;
}

void settingsPickPhrase(char* out, unsigned n) {
  if (!out || !n) return;
  out[0] = 0;
  // count how many candidates are switched on
  int on = 0;
  for (int i = 0; i < PHRASE_MAX; i++) if (cfg.phraseOn[i]) on++;
  if (!on) {                                   // nothing enabled -> fall back
    const char* d = PHRASES[0];
    strncpy(out, d, n - 1); out[n - 1] = 0;
    return;
  }
  int pick = random(on);
  for (int i = 0; i < PHRASE_MAX; i++) {
    if (cfg.phraseOn[i] && pick-- == 0) {
      strncpy(out, PHRASES[i], n - 1); out[n - 1] = 0;
      return;
    }
  }
}
