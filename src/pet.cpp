// =====================================================================
//  pet.cpp  -  electronic pet state machine
// =====================================================================
#include "pet.h"
#include "settings.h"      // cfg.growthPct: user selectable growth speed
#include <Preferences.h>   // NVS persistence (used when USE_SD == 0)

#if USE_SD
#include <SD.h>
#endif

// Age thresholds, in age "minutes". At 100% growth speed one age minute is
// exactly one minute of wall clock (see tick()), so these are also real minutes:
// 30 min to 小孩, 2 h to 少年, 6 h to 成年. Scale the whole schedule with the
// WiFi page's 成长速度 (20..400%) instead of editing them one by one.
#define AGE_CHILD 30
#define AGE_TEEN  120
#define AGE_ADULT 360
// Length of one age minute at 100% growth speed. The older model counted one
// *tick* (PET_TICK_MS = 2 s) as an age minute, i.e. the age ran 30x faster than
// the clock and the status page read "龄 3000m" after one hour of play. The
// accumulator in tick() is wall-clock based instead.
#define AGE_MIN_MS 60000UL
static_assert((AGE_MIN_MS % PET_TICK_MS) == 0,
              "one age minute must be a whole number of ticks");
#define SAVE_MAGIC 0x50455431UL   // 'PET1'

const char* stageName(uint8_t stage) {
  switch (stage) {
    case STAGE_EGG:   return "蛋";
    case STAGE_BABY:  return "婴儿";
    case STAGE_CHILD: return "小孩";
    case STAGE_TEEN:  return "少年";
    case STAGE_ADULT: return "成年";
  }
  return "?";
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v)); }

// Age as text for the status page: Chinese units (分 / 时 / 天) like the rest of
// the interface, and exactly ONE unit at a time. The data pages give a value
// only 38 px of room (app_lvgl.cpp: colW - 26, the row's dim label owns the
// rest), a CJK cell is 16 px, so "6天6时" would already be clipped - and a single
// unit also can never run away the way "4600m" did. The exact minute count is
// printed by the boot log instead of being squeezed into the row.
// 分 / 时 / 天 exist in both baked fonts (tools/gen_*font.ps1, which scan the
// sources for non-ASCII characters); any other character added here means
// re-running those two scripts, otherwise it draws blank.
void petAgeText(char* out, unsigned n, uint16_t ageMin) {
  if (!out || !n) return;
  if (ageMin < 60)         snprintf(out, n, "%u分", (unsigned)ageMin);
  else if (ageMin < 1440)  snprintf(out, n, "%u时", (unsigned)(ageMin / 60));
  else                     snprintf(out, n, "%u天", (unsigned)(ageMin / 1440));
}

// One line for the boot log. The growth speed lives in NVS and can therefore
// differ between two boards running the same firmware, so the pace is printed
// instead of being hidden in the sources - the answer to "why is my pet ageing
// so fast" should be readable in the serial output.
void petGrowthPlanText(char* out, unsigned n) {
  if (!out || !n) return;
  const uint32_t pct = cfg.growthPct ? cfg.growthPct : 100;      // never divide by 0
  const uint32_t msPerMin = (AGE_MIN_MS * 100UL) / pct;          // real ms per age minute
  snprintf(out, n, "1 age-min per %lu s (speed %lu%%), %u min to child, %lu h to adult",
           (unsigned long)(msPerMin / 1000UL), (unsigned long)pct,
           (unsigned)AGE_CHILD, (unsigned long)((uint32_t)AGE_ADULT * msPerMin / 3600000UL));
}

// --------------------------------------------------------------- lifecycle
void Pet::begin() { reset(); }

void Pet::reset() {
  _st.hunger = 80;
  _st.happy  = 80;
  _st.energy = 90;
  _st.health = 100;
  _st.clean  = 100;
  _st.weight = 10;
  _st.ageMin = 0;
  _st.stage  = STAGE_BABY;
  _st.poop   = 0;
  _st.asleep = false;
  _st.sick   = false;
  _st.dead   = false;
  _st.poopsTotal = 0;
  _st.mistakes   = 0;
  _acc = 0; _ticks = 0; _ageAcc = 0;
  _poopCountdown = random(90, 220);
  _sickCountdown = 300;
  _evolved = false; _pooped = false;
}

void Pet::update(uint32_t dt) {
  if (_st.dead) return;
  _acc += dt;
  while (_acc >= PET_TICK_MS) { _acc -= PET_TICK_MS; tick(); }
}

void Pet::tick() {
  _ticks++;
  // Ageing follows the wall clock: `cfg.growthPct` percent of one age minute per
  // real minute (100 = normal, 20 = five times slower, 400 = four times faster -
  // the WiFi page exposes this as 成长速度). The accumulator holds "ms * percent"
  // so the rate stays exact at every speed and no tick is lost to rounding.
  _ageAcc += (uint32_t)PET_TICK_MS * cfg.growthPct;
  while (_ageAcc >= AGE_MIN_MS * 100UL) {
    _ageAcc -= AGE_MIN_MS * 100UL;
    if (_st.ageMin < 65000) _st.ageMin++;
  }

  if (_st.asleep) {
    _st.energy = clamp8(_st.energy + ENERGY_SLEEP_GAIN);
    if ((_ticks % HUNGER_PERIOD_TICKS) == 0) _st.hunger = clamp8(_st.hunger - 1);
  } else {
    if ((_ticks % HUNGER_PERIOD_TICKS) == 0) _st.hunger = clamp8(_st.hunger - 1);
    if ((_ticks % HAPPY_PERIOD_TICKS)  == 0) _st.happy  = clamp8(_st.happy  - 1);
    if ((_ticks % ENERGY_PERIOD_TICKS) == 0) _st.energy = clamp8(_st.energy - 1);
  }

  // slow dirt build-up
  if ((_ticks % CLEAN_PERIOD_TICKS) == 0) _st.clean = clamp8(_st.clean - 1);

  // poop
  if (--_poopCountdown <= 0) {
    if (!_st.asleep && _st.poop < 4) addPoop();
    _poopCountdown = random(90, 220);
  }

  // random sickness when neglected
  if (--_sickCountdown <= 0) {
    if (!_st.sick && !_st.asleep && (_st.hunger < 25 || _st.clean < 25) && random(100) < 25)
      _st.sick = true;
    _sickCountdown = random(180, 500);
  }

  // health drift (moves at most one step every HEALTH_PERIOD_TICKS ticks)
  if ((_ticks % HEALTH_PERIOD_TICKS) == 0) {
    int worst = _st.hunger;
    if (_st.clean  < worst) worst = _st.clean;
    if (_st.happy  < worst) worst = _st.happy;
    if (_st.energy < worst) worst = _st.energy;
    if (_st.sick) worst -= 20;
    if (_st.poop > 0) worst -= 8 * _st.poop;

    if (worst < 25)      _st.health = clamp8(_st.health - 1);
    else if (worst > 60) _st.health = clamp8(_st.health + 1);
  }

  if (_st.health == 0) _st.dead = true;

  recomputeStage(true);
}

void Pet::addPoop() {
  _st.poop++;
  _st.poopsTotal++;
  _st.clean = clamp8(_st.clean - 12);
  _st.happy = clamp8(_st.happy - 3);
  if (_st.poop >= 3) _st.mistakes++;
  _pooped = true;
}

void Pet::recomputeStage(bool announce) {
  uint8_t ns;
  if (_st.ageMin >= AGE_ADULT)      ns = STAGE_ADULT;
  else if (_st.ageMin >= AGE_TEEN)  ns = STAGE_TEEN;
  else if (_st.ageMin >= AGE_CHILD) ns = STAGE_CHILD;
  else                              ns = STAGE_BABY;

  if (ns > _st.stage) { _st.stage = ns; if (announce) _evolved = true; }
}

// --------------------------------------------------------------- actions
// Each care action reports *what happened* instead of silently doing nothing, so
// the UI can answer with the matching face / speech bubble (see careaction.h).
CareOutcome Pet::feed() {
  if (_st.dead)   return CARE_OUT_DEAD;
  if (_st.asleep) return CARE_OUT_ASLEEP;
  if (_st.hunger >= 95) { _st.mistakes++; addHappy(-4); return CARE_OUT_FULL; }   // over-fed
  _st.hunger = clamp8(_st.hunger + (_st.stage == STAGE_BABY ? 30 : 22));
  _st.weight = (uint8_t)min(255, _st.weight + 1);
  addHappy(2);
  return CARE_OUT_OK;
}

void Pet::play() {
  if (_st.dead || _st.asleep) return;
  if (_st.energy < 8) return;
  _st.happy  = clamp8(_st.happy + 16);
  _st.energy = clamp8(_st.energy - 8);
  _st.hunger = clamp8(_st.hunger - 4);
  _st.weight = (uint8_t)max(5, (int)_st.weight - 1);
}

void Pet::toggleSleep() { if (!_st.dead) _st.asleep = !_st.asleep; }
void Pet::wake()        { _st.asleep = false; }

CareOutcome Pet::wash() {
  if (_st.dead) return CARE_OUT_DEAD;
  _st.poop = 0;
  _st.clean = 100;
  addHappy(2);
  return CARE_OUT_OK;
}

CareOutcome Pet::medicine() {
  if (_st.dead) return CARE_OUT_DEAD;
  _st.sick = false;
  _st.health = clamp8(_st.health + 25);
  return CARE_OUT_OK;
}

void Pet::petIt() { if (!_st.dead) addHappy(3); }

void Pet::scold() {
  if (_st.dead) return;
  _st.happy = clamp8(_st.happy - 5);
  if (_st.mistakes) _st.mistakes--;
}

void Pet::addHappy(int d)  { _st.happy = clamp8((int)_st.happy + d); }
void Pet::addHunger(int d) { _st.hunger = clamp8((int)_st.hunger + d); }

// --------------------------------------------------------------- status
int Pet::moodScore() const {
  if (_st.dead) return 0;
  int v = ((int)_st.hunger + _st.happy + _st.energy + _st.health + _st.clean) / 5;
  if (_st.sick) v -= 20;
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}

EyeMood Pet::expressionMood() const {
  if (_st.dead) return MOOD_DEAD;
  if (_st.asleep) return MOOD_SLEEPY;
  if (_st.sick) return MOOD_SICK;
  if (_st.clean < 25) return MOOD_ANGRY;
  if (_st.hunger < 15 || _st.happy < 15 || _st.energy < 10) return MOOD_SAD;
  if (_st.happy >= 85 && _st.hunger > 50) return MOOD_LOVE;
  if (_st.happy >= 65) return MOOD_HAPPY;
  if (_st.hunger < 35 || _st.happy < 35) return MOOD_CURIOUS;
  return MOOD_NEUTRAL;
}

bool Pet::consumeEvolve() { bool e = _evolved; _evolved = false; return e; }
bool Pet::consumePoop()   { bool p = _pooped;  _pooped  = false; return p; }

// --------------------------------------------------------------- storage
// NVS keeps the pet across power cycles even without an SD card. The state is
// tiny (~20 bytes) and NVS performs wear-levelling, so a slow autosave is fine.
#if !USE_SD
static Preferences s_prefs;
#endif

bool Pet::save() {
#if USE_SD
  File f = SD.open("/pet.dat", FILE_WRITE);
  if (!f) return false;
  uint32_t magic = SAVE_MAGIC;
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&_st, sizeof(_st));
  f.close();
  return true;
#else
  s_prefs.begin("pet", false);
  s_prefs.putUInt("magic", (uint32_t)SAVE_MAGIC);
  size_t n = s_prefs.putBytes("st", &_st, sizeof(_st));
  s_prefs.end();
  return n == sizeof(_st);
#endif
}

bool Pet::load() {
#if USE_SD
  File f = SD.open("/pet.dat", FILE_READ);
  if (!f) return false;
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != SAVE_MAGIC) {
    f.close(); return false;
  }
  size_t n = f.read((uint8_t*)&_st, sizeof(_st));
  f.close();
  if (n != sizeof(_st)) return false;
  recomputeStage(false);
  return true;
#else
  s_prefs.begin("pet", true);
  uint32_t magic = s_prefs.getUInt("magic", 0);
  size_t n = 0;
  if (magic == (uint32_t)SAVE_MAGIC) n = s_prefs.getBytes("st", &_st, sizeof(_st));
  s_prefs.end();
  if (n != sizeof(_st)) return false;
  recomputeStage(false);
  return true;
#endif
}
