// =====================================================================
//  buzzer.h  -  passive buzzer driver (tone / LEDC) + non blocking player
// =====================================================================
#pragma once
#include <Arduino.h>

// Sound effect / song ids -------------------------------------------------
enum {
  SND_CLICK = 0,
  SND_MOVE,
  SND_CONFIRM,
  SND_BACK,
  SND_ERROR,
  SND_EAT,
  SND_HAPPY,
  SND_SAD,
  SND_SLEEP,
  SND_EVOLVE,
  SND_STARTUP,
  SND_POOP,
  SND_SONG1,     // Ode to Joy
  SND_SONG2,     // Twinkle
  SND_SONG3,     // Birthday
  SND_COUNT
};

void buzzerInit();
void buzzerUpdate();                 // call every loop()

void buzzerSetMute(bool mute);
bool buzzerMuted();
void buzzerToggleMute();

void buzzerStop();
void buzzerBeep(uint16_t freq, uint16_t ms);   // single short tone
void buzzerPlay(uint8_t id, bool loop);        // play a sound/song by id
bool buzzerBusy();                             // a song is playing

const char* buzzerName(uint8_t id);

// convenience wrappers ----------------------------------------------------
inline void sfxClick()   { buzzerBeep(1300, 18); }
inline void sfxMove()    { buzzerBeep(950,  15); }
inline void sfxConfirm() { buzzerBeep(1800, 35); }
inline void sfxCancel()  { buzzerPlay(SND_BACK,    false); }
inline void sfxError()   { buzzerPlay(SND_ERROR,   false); }
inline void sfxEat()     { buzzerPlay(SND_EAT,     false); }
inline void sfxHappy()   { buzzerPlay(SND_HAPPY,   false); }
inline void sfxSad()     { buzzerPlay(SND_SAD,     false); }
inline void sfxSleep()   { buzzerPlay(SND_SLEEP,   false); }
inline void sfxEvolve()  { buzzerPlay(SND_EVOLVE,  false); }
inline void sfxStartup() { buzzerPlay(SND_STARTUP, false); }
inline void sfxPoop()    { buzzerPlay(SND_POOP,    false); }
