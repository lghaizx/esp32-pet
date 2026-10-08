// =====================================================================
//  buttons.cpp
// =====================================================================
#include "buttons.h"

static const uint8_t  pins[BTN_COUNT] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B };

static bool     stable[BTN_COUNT];
static bool     prevStable[BTN_COUNT];
static uint32_t tChange[BTN_COUNT];
static uint32_t tPress[BTN_COUNT];
static bool     fPress[BTN_COUNT];
static bool     fRelease[BTN_COUNT];
static bool     idle[BTN_COUNT];      // raw pin level when the key is NOT pressed

// GPIO34/35/36/39 have no internal pull resistors -> they need an external one.
static inline bool hasInternalPull(uint8_t pin) {
  return !(pin == 34 || pin == 35 || pin == 36 || pin == 39);
}

// Give each key the strongest available pull toward its idle rail. Pressing a
// key always drives the pin AWAY from that rail, so the idle level is learned
// per pin below (this also copes with external pull-ups on 34/35).
static inline void applyPull(uint8_t pin) {
  if (hasInternalPull(pin)) pinMode(pin, BTN_PRESSED_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  else                      pinMode(pin, INPUT);      // relies on the external resistor
}

static inline bool readActive(uint8_t i) {
  return digitalRead(pins[i]) != idle[i];             // departure from idle == pressed
}

void buttonsInit() {
#if BTN_DEBUG
  // Wiring probe (runs once): drive each pin HIGH then LOW and read it back.
  // This reveals the external resistors without needing any key press.
  static const char* const nm[BTN_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B" };
  Serial.println("[btn] probe  pin    up dn  verdict");
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    uint8_t pin = pins[i];
    pinMode(pin, INPUT_PULLUP);   delay(3); int u = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN); delay(3); int d = digitalRead(pin);
    const char* verdict =
        !hasInternalPull(pin) ? "no internal pull -> external R decides"
      : (u && !d) ? "floating           -> GND switch  (want PULLUP)"
      : (u &&  d) ? "external pull-up   -> GND switch"
      : (!u && !d) ? "external pull-down -> VCC switch?"
      : "unexpected";
    Serial.printf("[btn]   %-5s GPIO%-2u  %d  %d   %s\n", nm[i], pin, u, d, verdict);
  }
  Serial.println("[btn] probe end");
#endif

  for (uint8_t i = 0; i < BTN_COUNT; i++) applyPull(pins[i]);
  delay(5);                                           // let the levels settle

#if BTN_AUTO_POLARITY
  // Learn each pin's idle level with a majority vote so a momentary glitch or a
  // single noisy sample can't flip the polarity.
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    uint8_t hi = 0;
    for (uint8_t k = 0; k < 9; k++) { if (digitalRead(pins[i])) hi++; delayMicroseconds(300); }
    idle[i] = (hi >= 5);
  }
#else
  for (uint8_t i = 0; i < BTN_COUNT; i++) idle[i] = BTN_PRESSED_HIGH ? 0 : 1;
#endif

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    stable[i] = prevStable[i] = readActive(i);
    tChange[i] = millis();
    tPress[i]  = 0;
    fPress[i]  = fRelease[i] = false;
  }

#if BTN_DEBUG
  Serial.printf("[btn] idle UP=%d DOWN=%d LEFT=%d RIGHT=%d A=%d B=%d\n",
                idle[BTN_UP_I], idle[BTN_DOWN_I], idle[BTN_LEFT_I],
                idle[BTN_RIGHT_I], idle[BTN_A_I], idle[BTN_B_I]);
#endif
}

void buttonsEndFrame() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) { fPress[i] = false; fRelease[i] = false; }
}

void buttonsUpdate() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    bool v = readActive(i);
    if (v != stable[i]) {
      if (now - tChange[i] >= BTN_DEBOUNCE_MS) {
        prevStable[i] = stable[i];
        stable[i]     = v;
        tChange[i]    = now;
        if (stable[i] && !prevStable[i]) { fPress[i] = true; tPress[i] = now; }
        if (!stable[i] && prevStable[i]) { fRelease[i] = true; }
      }
    } else {
      tChange[i] = now;    // reset timer while steady
    }
  }
}

bool btnDown(uint8_t i)     { return i < BTN_COUNT && stable[i]; }
bool btnPressed(uint8_t i)  { return i < BTN_COUNT && fPress[i]; }
bool btnReleased(uint8_t i) { return i < BTN_COUNT && fRelease[i]; }

bool btnHeld(uint8_t i, uint32_t ms) {
  return btnDown(i) && (millis() - tPress[i] >= ms);
}
uint32_t btnHeldMs(uint8_t i) {
  return btnDown(i) ? (millis() - tPress[i]) : 0;
}
bool anyBtnPressed() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) if (fPress[i]) return true;
  return false;
}
