// =====================================================================
//  sensors.cpp
// =====================================================================
#include "sensors.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

// NTC parameters (typical 10k @25C, B=3950) with a 10k series resistor to GND.
#define NTC_R0      10000.0f
#define NTC_T0      298.15f
#define NTC_BETA    3950.0f
#define NTC_SERIES  10000.0f
#define ADC_VREF    3.3f
#define ADC_MAX     4095.0f

static int    s_lightRaw = 0, s_lightPct = 0;
static int    s_tempRaw  = 0, s_tempDeci = 250;
static float  s_lightF   = 0, s_tempF = 25.0f;
static bool   s_inited   = false;

// ---- "cover me" gesture state (see sensorsCoverEvent()) ----
static float    s_lightRef  = 0.0f;     // ambient light: peak-hold, snaps up / decays slowly
static float    s_tempRef   = 25.0f;    // slow ambient temperature reference
static bool     s_dropping  = false;    // light has fallen below the cover ratio
static uint32_t s_gestureStart = 0;     // millis() the cover began
static bool     s_fired     = false;    // already reacted to this cover
static bool     s_coverEvent = false;   // one-shot latch for the caller
static bool     s_lightPrimed = false;  // seeded from the first real reading
static bool     s_tempPrimed  = false;  //   (no artificial boot-time "rise")

void sensorsInit() {
  pinMode(LDR_PIN, INPUT);
  pinMode(NTC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);   // full 0..3.3V range
  analogSetPinAttenuation(NTC_PIN, ADC_11db);
  // prime the filter
  s_lightF = analogRead(LDR_PIN);
  s_tempF  = 25.0f;
  s_inited = true;
}

void sensorsUpdate() {
  if (!s_inited) sensorsInit();

  // ---- light (simple low-pass) ----
  int lr = analogRead(LDR_PIN);
  if (!s_lightPrimed) { s_lightF = s_lightRef = (float)lr; s_lightPrimed = true; }
  s_lightF += (lr - s_lightF) * 0.25f;
  s_lightRaw = (int)s_lightF;
  s_lightPct = (int)(s_lightF / ADC_MAX * 100.0f + 0.5f);

  // ---- temperature ----
  s_tempRaw = analogRead(NTC_PIN);
  float v = s_tempRaw / ADC_MAX * ADC_VREF;
  if (v > 0.05f && v < (ADC_VREF - 0.05f)) {
    float r = NTC_SERIES * (ADC_VREF / v - 1.0f);   // NTC to VREF, R to GND
    if (r > 50.0f) {
      float t = 1.0f / (1.0f / NTC_T0 + (1.0f / NTC_BETA) * logf(r / NTC_R0));
      t -= 273.15f;
      if (t > -40.0f && t < 125.0f) {
        // Seed both filters from the first real reading, otherwise the default
        // 25.0 C would look like a genuine warm-up and fake a cover gesture.
        if (!s_tempPrimed) { s_tempF = s_tempRef = t; s_tempPrimed = true; }
        s_tempF += (t - s_tempF) * 0.15f;
      }
    }
  }
  s_tempDeci = (int)(s_tempF * 10.0f + (s_tempF >= 0 ? 0.5f : -0.5f));

  // ---- "cover me" gesture ----
  // A hand over the sensors drives the LDR to (near) zero: on the bench, idle is
  // 27..31 % and a cupped hand is 0..2 %, so the absolute dark test below is what
  // really detects the gesture. The temperature test is kept as a (deliberately
  // weak) secondary condition - see the note in config.h.
  const uint32_t now = millis();

  if (!s_dropping) {
    // Ambient light reference: a peak-hold. It snaps up to any brighter reading
    // immediately and then decays only very slowly, so it always remembers how
    // bright the room was. The old code re-sampled it outright every 2 s instead,
    // which could catch a *half covered* value (a logged 4 % while the room was
    // 30 %): the re-arm threshold then fell to ~2 %, so a hand left resting on the
    // sensors could re-arm and fire over and over. Freezing this while a cover is
    // in progress is what keeps a long hold to a single reaction.
    if (s_lightF > s_lightRef) s_lightRef = s_lightF;
    else s_lightRef += (s_lightF - s_lightRef) * COVER_AMB_DECAY;
    // Reference temperature, learned very slowly. NOTE: ordinary ambient drift
    // already keeps this lagging by 0.1..0.4 C, which is at or above
    // COVER_RISE_DECI - so the condition is effectively always true and never
    // vetoes a real cover (measured: the NTC barely notices a hand).
    s_tempRef += (s_tempF - s_tempRef) * 0.01f;
  }

  const bool dark = s_lightPct <= COVER_DARK_PCT;   // light sensor reads ~zero (hand over it)
  if (!s_dropping) {
    if (dark) { s_dropping = true; s_gestureStart = now ? now : 1; s_fired = false; }
  } else if (s_lightF > s_lightRef * (COVER_REARM / 100.0f)) {
    s_dropping = false;                             // light came back -> re-arm
  }

  if (s_dropping) {
    const bool rising = (s_tempF - s_tempRef) >= (COVER_RISE_DECI / 10.0f);
    const bool held   = (uint32_t)(now - s_gestureStart) >= COVER_HOLD_MS;
    if (!s_fired && rising && held) {
      s_fired      = true;
      s_coverEvent = true;                          // fire the reaction once
    }
  }

#if COVER_DEBUG
  // Live trace of the detector so the sensor behaviour can be watched in the
  // serial monitor (~once a second). "amb" is the ambient light baseline and
  // "rise" is how far the temperature has climbed above its reference.
  static uint32_t dbgMs = 0;
  if ((uint32_t)(now - dbgMs) >= 1000) {
    dbgMs = now;
    const float refPct = s_lightRef / ADC_MAX * 100.0f;
    const int td = s_tempDeci;
    const int rd = (int)(s_tempRef * 10.0f + (s_tempRef >= 0 ? 0.5f : -0.5f));
    Serial.printf("[cover] light=%d%% amb=%.0f%% temp=%d.%dC ambT=%d.%dC rise=%.2f %s%s\n",
                  s_lightPct, refPct, td / 10, abs(td % 10), rd / 10, abs(rd % 10),
                  s_tempF - s_tempRef, s_dropping ? "COVERED" : "idle",
                  s_fired ? "  -> 好舒服啊" : "");
  }
#endif
}

int lightRaw()  { return s_lightRaw; }
int lightPct()  { return s_lightPct; }
int tempRaw()   { return s_tempRaw; }
int tempDeciC() { return s_tempDeci; }

void lightText(char* out, unsigned n) {
  int p = s_lightPct;
  const char* s = (p > 66) ? "亮" : (p > 25) ? "中" : "暗";
  snprintf(out, n, "%s %d%%", s, p);
}

void tempText(char* out, unsigned n) {
  int d = s_tempDeci;
  snprintf(out, n, "%d.%dC", d / 10, abs(d % 10));
}

// --- compact corner HUD ---------------------------------------------------
// Short enough to sit in the scene's top corners next to the pet's eyebrows.
void lightHudText(char* out, unsigned n) {
  snprintf(out, n, "光%d%%", s_lightPct);
}

void tempHudText(char* out, unsigned n) {
  int d = s_tempDeci;
  snprintf(out, n, "温%d.%d", d / 10, abs(d % 10));
}

// --- "cover me" gesture ---------------------------------------------------
bool sensorsCoverEvent() {
  if (!s_coverEvent) return false;
  s_coverEvent = false;      // consumed: one reaction per gesture
  return true;
}

bool sensorsHandCovered() { return s_dropping; }
