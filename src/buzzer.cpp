// =====================================================================
//  buzzer.cpp  -  passive buzzer on BUZZER_PIN via Arduino tone()/LEDC
// =====================================================================
#include "buzzer.h"
#include "config.h"

// ---- note frequencies (Hz) ----
#define C4 262
#define D4 294
#define E4 330
#define F4 349
#define G4 392
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define F5 698
#define G5 784
#define A5 880
#define B5 988
#define C6 1047
#define R  0     // rest

struct Note { uint16_t f; uint16_t d; };

// ---- sound effect jingles ----
static const Note sClick[]   = {{1300,18},{R,2}};
static const Note sMove[]    = {{950,15},{R,2}};
static const Note sConfirm[] = {{1600,30},{2000,45},{R,2}};
static const Note sBack[]    = {{700,40},{500,55},{R,2}};
static const Note sError[]   = {{300,90},{200,140},{R,2}};
static const Note sEat[]     = {{700,50},{1000,50},{1300,70},{R,2}};
static const Note sHappy[]   = {{C5,70},{E5,70},{G5,70},{C6,130},{R,2}};
static const Note sSad[]     = {{A4,140},{G4,140},{F4,140},{E4,260},{R,2}};
static const Note sSleep[]   = {{G4,120},{E4,120},{C4,200},{R,2}};
static const Note sEvolve[]  = {{C5,90},{E5,90},{G5,90},{C6,120},{G5,90},{C6,260},{R,2}};
static const Note sStartup[] = {{C5,90},{E5,90},{G5,90},{C6,160},{G5,90},{C6,300},{R,2}};
static const Note sPoop[]    = {{220,60},{160,120},{R,2}};

// ---- songs ----
static const Note odeToJoy[] = {
  {E5,150},{E5,150},{F5,150},{G5,150},{G5,150},{F5,150},{E5,150},{D5,150},
  {C5,150},{C5,150},{D5,150},{E5,150},{E5,200},{D5,80},{D5,250},{R,40},
  {E5,150},{E5,150},{F5,150},{G5,150},{G5,150},{F5,150},{E5,150},{D5,150},
  {C5,150},{C5,150},{D5,150},{E5,150},{D5,200},{C5,80},{C5,300},{R,60}
};
static const Note twinkle[] = {
  {C5,200},{C5,200},{G5,200},{G5,200},{A5,200},{A5,200},{G5,400},{R,40},
  {F5,200},{F5,200},{E5,200},{E5,200},{D5,200},{D5,200},{C5,400},{R,60}
};
static const Note birthday[] = {
  {C5,150},{C5,100},{D5,300},{C5,300},{F5,300},{E5,450},{R,40},
  {C5,150},{C5,100},{D5,300},{C5,300},{G5,300},{F5,450},{R,60}
};

struct Sound { const Note* notes; uint16_t n; };

static Sound tabla[SND_COUNT] = {
  {sClick,  sizeof(sClick)/sizeof(Note)},
  {sMove,   sizeof(sMove)/sizeof(Note)},
  {sConfirm,sizeof(sConfirm)/sizeof(Note)},
  {sBack,   sizeof(sBack)/sizeof(Note)},
  {sError,  sizeof(sError)/sizeof(Note)},
  {sEat,    sizeof(sEat)/sizeof(Note)},
  {sHappy,  sizeof(sHappy)/sizeof(Note)},
  {sSad,    sizeof(sSad)/sizeof(Note)},
  {sSleep,  sizeof(sSleep)/sizeof(Note)},
  {sEvolve, sizeof(sEvolve)/sizeof(Note)},
  {sStartup,sizeof(sStartup)/sizeof(Note)},
  {sPoop,   sizeof(sPoop)/sizeof(Note)},
  {odeToJoy,sizeof(odeToJoy)/sizeof(Note)},
  {twinkle, sizeof(twinkle)/sizeof(Note)},
  {birthday,sizeof(birthday)/sizeof(Note)},
};

static const char* names[SND_COUNT] = {
  "Click","Move","Confirm","Back","Error","Eat","Happy","Sad",
  "Sleep","Evolve","Startup","Poop","欢乐颂","小星星","生日快乐"
};

// ---- player state ----
static bool     s_muted   = false;
static uint16_t s_idx     = 0;
static uint32_t s_noteAt  = 0;
static bool     s_loop    = false;
static bool     s_playing = false;
static const Sound* s_cur = nullptr;

// ---- single beep state ----
static bool     s_beep    = false;
static uint32_t s_beepEnd = 0;

void buzzerInit() {
  // Create LEDC channel 0 (the one Arduino's tone() uses, see Tone.cpp) before
  // the first tone() call. tone() hands the pin to LEDC *before* it sets the
  // frequency, and ledcAttachPin() reads the channel's duty with ledc_get_duty()
  // - which, on a channel that was never set up, trips the IDF's LEDC_CHECK and
  // prints
  //     E (xxx) ledc: ledc_get_duty(202): LEDC is not initialized
  // once per boot. ledcSetup() allocates that channel object (it goes through
  // ledc_timer_config()), so the query succeeds; ledcWriteTone() that follows
  // from tone() overwrites the frequency and duty again, i.e. this line only
  // silences the log.
  ledcSetup(0, 1000, 10);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  s_muted = false;
}

void buzzerStop() {
  noTone(BUZZER_PIN);
  s_playing = false;
  s_beep    = false;
  s_cur     = nullptr;
}

void buzzerSetMute(bool m) {
  s_muted = m;
  if (m) buzzerStop();
}

bool buzzerMuted() { return s_muted; }

void buzzerToggleMute() { buzzerSetMute(!s_muted); }

bool buzzerBusy() { return s_playing || s_beep; }

void buzzerBeep(uint16_t freq, uint16_t ms) {
  if (s_muted) return;
  if (s_playing) return;               // don't clip a song with a blip
  tone(BUZZER_PIN, freq);
  s_beep    = true;
  s_beepEnd = millis() + ms;
}

void buzzerPlay(uint8_t id, bool loop) {
  if (s_muted || id >= SND_COUNT) return;
  s_cur     = &tabla[id];
  s_idx     = 0;
  s_loop    = loop;
  s_playing = s_cur->n > 0;
  s_beep    = false;
  if (!s_playing) return;
  if (s_cur->notes[0].f == R) noTone(BUZZER_PIN);
  else                       tone(BUZZER_PIN, s_cur->notes[0].f);
  s_noteAt = millis();
}

void buzzerUpdate() {
  if (s_muted) return;
  uint32_t now = millis();

  // ---- song player ----
  if (s_playing && s_cur) {
    if (now - s_noteAt >= s_cur->notes[s_idx].d) {
      s_idx++;
      if (s_idx >= s_cur->n) {
        if (s_loop) s_idx = 0;
        else { s_playing = false; noTone(BUZZER_PIN); return; }
      }
      if (s_cur->notes[s_idx].f == R) noTone(BUZZER_PIN);
      else                            tone(BUZZER_PIN, s_cur->notes[s_idx].f);
      s_noteAt = now;
    }
    return;
  }

  // ---- single beep ----
  if (s_beep && now >= s_beepEnd) {
    noTone(BUZZER_PIN);
    s_beep = false;
  }
}

const char* buzzerName(uint8_t id) { return (id < SND_COUNT) ? names[id] : "?"; }
