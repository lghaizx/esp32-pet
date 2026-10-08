// =====================================================================
//  lv_port.h  -  LVGL <-> ESP32 glue (display flush + 6-key input)
// =====================================================================
#pragma once
#include <lvgl.h>
#include <TFT_eSPI.h>

// Bring LVGL up: heap, display driver (draw buffer + flush callback) and the
// keypad input device. `tft` must already be init()'d and rotated.
void        lv_port_init(TFT_eSPI* tft);

// Scan the 6 buttons and queue every fresh press as an LVGL key. Call once
// per main-loop frame, right after buttonsUpdate() and before the edges are
// cleared by buttonsEndFrame().
void        lv_port_poll_keys();

// Drop every key that is still waiting in the queue. Called on each screen
// change so that a key handled by the screen we are leaving can never trigger
// a widget on the screen we are entering.
void        lv_port_clear_keys();

// The focus group that receives the keypad input; add objects to it.
lv_group_t* lv_port_group();
