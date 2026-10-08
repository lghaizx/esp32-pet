// =====================================================================
//  sensors.h  -  LDR (GPIO36) + NTC thermistor (GPIO39) on ADC1
// =====================================================================
#pragma once
#include <stdint.h>

void sensorsInit();
void sensorsUpdate();          // sample + smooth (call ~ every 100ms)

int  lightRaw();               // 0 .. 4095
int  lightPct();               // 0 .. 100 (% brightness, smoothed)

int  tempRaw();                // 0 .. 4095
int  tempDeciC();              // temperature * 10 (e.g. 235 == 23.5 C)

// text helpers
void lightText(char* out, unsigned n);   // "亮 82%"  (with a word for the level)
void tempText(char* out, unsigned n);    // "23.5C"

// Compact read-outs for the corner HUD that EyeEngine paints on the pet scene:
// light in the top left, temperature in the top right. Short on purpose - they
// share the first row of the scene with the pet's eyebrows.
void lightHudText(char* out, unsigned n);   // "光82%"
void tempHudText (char* out, unsigned n);   // "温23.5"

// "Cover me" gesture: the light sensor reads (near) zero while the slow ambient
// reference is still held up, i.e. a hand is cupped over it (the temperature side
// is only a weak secondary condition - see config.h). Returns true exactly once
// per cover, so the caller can have the pet react ("好舒服啊").
// Only meaningful after sensorsUpdate() runs.
bool sensorsCoverEvent();

// Level of the gesture: true while a hand is still held over the sensors.
bool sensorsHandCovered();
