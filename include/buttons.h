// =====================================================================
//  buttons.h  -  6-key input with debounce, edge + long-press support
// =====================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// index -> physical key
enum {
  BTN_UP_I = 0,   // GPIO2
  BTN_DOWN_I,     // GPIO13
  BTN_LEFT_I,     // GPIO27
  BTN_RIGHT_I,    // GPIO35
  BTN_A_I,        // GPIO34
  BTN_B_I         // GPIO12
};

void buttonsInit();
void buttonsUpdate();              // call often; latches edges
void buttonsEndFrame();            // clear latched edges after handling input

bool btnDown(uint8_t i);           // held right now
bool btnPressed(uint8_t i);        // became pressed since last frame
bool btnReleased(uint8_t i);       // became released since last frame
bool btnHeld(uint8_t i, uint32_t ms);   // held at least ms
uint32_t btnHeldMs(uint8_t i);
bool anyBtnPressed();
