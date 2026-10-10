// =====================================================================
//  settings.h  -  user tunables, stored in NVS and edited over WiFi
//
//  These values are read live by the pet behaviour (see pet.cpp and
//  app_lvgl.cpp) so a change made in the web config page takes effect at
//  once, without a reboot. The one exception is the welcome ("boot") screen
//  text below - it is only drawn while booting, so it shows up on the next
//  power-up.
// =====================================================================
#pragma once
#include <stdint.h>

// Number of built-in catchphrase candidates. The sentences themselves live in
// settings.cpp as string literals, because the 16x16 bitmap font is baked at
// build time: a sentence typed at runtime could never be drawn, so the user
// *enables / disables* the built-in ones instead (that is the \"add / remove\"
// of the web page).
#define PHRASE_MAX 24

// Built-in welcome ("boot") screen greetings the user can pick on the web page.
// They are string literals for the same reason as the phrases above: the font
// generator bakes every non-ASCII character it finds in the sources, so a
// greeting chosen at runtime can actually be drawn (see settings.cpp BOOT_LINES).
// Index 0 is the "off" entry - the screen then shows the firmware version.
#define BOOT_LINE_MAX 6

struct PetCfg {
  uint32_t talkMinMs;              // idle chatter: earliest next line
  uint32_t talkMaxMs;              // idle chatter: latest   next line (0 = off)
  uint32_t exprMinMs;              // expression flair: earliest next mood
  uint32_t exprMaxMs;              // expression flair: latest   next mood
  uint16_t growthPct;              // growth speed, percent (100 = normal)
  uint8_t  phraseOn[PHRASE_MAX];   // 1 = this candidate may be spoken

  // Welcome ("boot") screen, edited over WiFi (see netconfig.cpp). The first
  // line is bootTitle - free ASCII typed on the phone ("" keeps FW_NAME); the
  // second line is BOOT_LINES[bootLine], a built-in greeting.
  char     bootTitle[24];
  uint8_t  bootLine;
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

// built-in welcome ("boot") screen greetings; index 0 is "off" ("" -> the
// welcome screen falls back to showing the firmware version)
int         settingsBootLineCount();             // always BOOT_LINE_MAX
const char* settingsBootLine(int i);             // "" when out of range / off
