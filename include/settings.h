// =====================================================================
//  settings.h  -  user tunables, stored in NVS and edited over WiFi
//
//  These values are read live by the pet behaviour (see pet.cpp and
//  app_lvgl.cpp) so a change made in the web config page takes effect at
//  once, without a reboot.
// =====================================================================
#pragma once
#include <stdint.h>

// Number of built-in catchphrase candidates. The sentences themselves live in
// settings.cpp as string literals, because the 16x16 bitmap font is baked at
// build time: a sentence typed at runtime could never be drawn, so the user
// *enables / disables* the built-in ones instead (that is the \"add / remove\"
// of the web page).
#define PHRASE_MAX 24

struct PetCfg {
  uint32_t talkMinMs;              // idle chatter: earliest next line
  uint32_t talkMaxMs;              // idle chatter: latest   next line (0 = off)
  uint32_t exprMinMs;              // expression flair: earliest next mood
  uint32_t exprMaxMs;              // expression flair: latest   next mood
  uint16_t growthPct;              // growth speed, percent (100 = normal)
  uint8_t  phraseOn[PHRASE_MAX];   // 1 = this candidate may be spoken
};

extern PetCfg cfg;

void settingsBegin();                 // load from NVS (falls back to defaults)
void settingsDefaults();              // reset cfg to the factory values
void settingsSave();                  // persist cfg to NVS

// built-in catchphrase candidates
int         settingsPhraseCount();               // always PHRASE_MAX
const char* settingsPhrase(int i);               // candidate i ("" out of range)
bool        settingsPhraseEnabled(int i);
void        settingsSetPhraseEnabled(int i, bool on);
void        settingsPickPhrase(char* out, unsigned n);   // a random enabled line
