// =====================================================================
//  lv_port.cpp  -  LVGL display + keypad glue for the ESP32 Pixel Pet
//
//  * display : a 160x40 px draw buffer flushed with TFT_eSPI pushImage()
//  * input   : the 6 on-board keys are fed to LVGL as a keypad device
//              (UP = LV_KEY_PREV, DOWN = LV_KEY_NEXT, A = ENTER, B = ESC,
//               LEFT / RIGHT are passed through as plain keys)
//
//  The byte order agreed here is: LVGL produces panel-order (byte swapped)
//  pixels (LV_COLOR_16_SWAP == 1) and TFT_eSPI does NOT swap again, while the
//  EyeEngine's sprite is kept little-endian for LVGL (SPRITE_SWAP_BYTES == 0).
//
//  The tick source is Arduino millis() (see lv_conf.h).
// =====================================================================
#include <Arduino.h>

#include "lv_port.h"
#include "config.h"
#include "buttons.h"

static TFT_eSPI* s_tft = nullptr;

// ------------------------------------------------------------------ display
// The 160x40 px draw buffer (12.8 KB) is malloc'd in lv_port_init() instead of
// being a static array. The scarce resource on this board is the *static* RAM
// area (.dram0), which LVGL's own pool shares - LV_MEM_SIZE is a static array
// too - and with both of them as arrays the pool could not be grown past ~61 KB
// before the linker ran out of .dram0. A plain ESP32 allocates the heap from
// internal RAM as well, so the panel transfer is unaffected.
#define DRAW_BUF_PX (SCR_W * 40)
static lv_disp_draw_buf_t s_drawBuf;
static lv_color_t*        s_buf1 = nullptr;

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  // The draw buffer already holds the byte order the ST7735 wants, because
  // LV_COLOR_16_SWAP == 1 (see lv_conf.h), so it is pushed through as is
  // (setSwapBytes(false) was applied once in lv_port_init()).
  s_tft->pushImage(area->x1, area->y1, w, h, (uint16_t*)px);
  lv_disp_flush_ready(drv);
}

// ------------------------------------------------------------------ keypad
// A tiny ring buffer so a button edge can never be lost when LVGL's input
// read timer and our main loop are not perfectly in lock-step.
#define KEYQ_LEN 8
static uint32_t s_keyQ[KEYQ_LEN];
static uint8_t  s_keyHead = 0, s_keyTail = 0;
static lv_group_t* s_group = nullptr;

static void key_push(uint32_t k) {
  const uint8_t n = (uint8_t)((s_keyTail + 1) % KEYQ_LEN);
  if (n == s_keyHead) return;                 // full -> drop
  s_keyQ[s_keyTail] = k;
  s_keyTail = n;
}

static uint32_t key_pop() {
  if (s_keyHead == s_keyTail) return 0;
  const uint32_t k = s_keyQ[s_keyHead];
  s_keyHead = (uint8_t)((s_keyHead + 1) % KEYQ_LEN);
  return k;
}

void lv_port_poll_keys() {
  // LVGL's keypad driver walks a focus group with PREV / NEXT and activates
  // the focused object on ENTER (ESC = cancel), so the two "vertical" keys are
  // mapped onto PREV/NEXT here. LEFT/RIGHT stay as themselves for widgets that
  // make use of them.
  if (btnPressed(BTN_UP_I))    key_push(LV_KEY_PREV);
  if (btnPressed(BTN_DOWN_I))  key_push(LV_KEY_NEXT);
  if (btnPressed(BTN_LEFT_I))  key_push(LV_KEY_LEFT);
  if (btnPressed(BTN_RIGHT_I)) key_push(LV_KEY_RIGHT);
  if (btnPressed(BTN_A_I))     key_push(LV_KEY_ENTER);
  if (btnPressed(BTN_B_I))     key_push(LV_KEY_ESC);
}

void lv_port_clear_keys() {
  s_keyHead = s_keyTail = 0;
}

static void keypad_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;
  // LVGL only acts on a key when it sees the RELEASED -> PRESSED edge, so every
  // key that is handed over is followed by exactly one RELEASED report. Without
  // it two keys queued in the same main-loop frame would be read back to back,
  // the second one would arrive while the first still "looks" pressed and LVGL
  // would silently drop it.
  static uint32_t s_pressedKey = 0;   // key whose PRESSED report is still fresh

  if (s_pressedKey) {
    data->key   = s_pressedKey;
    data->state = LV_INDEV_STATE_RELEASED;
    s_pressedKey = 0;
    return;
  }

  const uint32_t k = key_pop();
  if (k) {
    s_pressedKey = k;
    data->key    = k;
    data->state  = LV_INDEV_STATE_PRESSED;
  } else {
    data->key   = 0;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ------------------------------------------------------------------ init
void lv_port_init(TFT_eSPI* tft) {
  s_tft = tft;

  lv_init();
  // LVGL's buffer already holds swapped (panel order) pixels, so make sure
  // TFT_eSPI does not swap them a second time.
  tft->setSwapBytes(false);

  s_buf1 = (lv_color_t*)malloc(sizeof(lv_color_t) * DRAW_BUF_PX);
  if (s_buf1 == nullptr) {                    // 12.8 KB - never seen on a WROOM
    Serial.println("[lv] no RAM for the draw buffer");
    while (true) delay(1000);
  }
  lv_disp_draw_buf_init(&s_drawBuf, s_buf1, NULL, DRAW_BUF_PX);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res  = SCR_W;
  dispDrv.ver_res  = SCR_H;
  dispDrv.flush_cb = disp_flush;
  dispDrv.draw_buf = &s_drawBuf;
  lv_disp_drv_register(&dispDrv);       // also installs the default theme

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type    = LV_INDEV_TYPE_KEYPAD;
  indevDrv.read_cb = keypad_read;
  lv_indev_t* indev = lv_indev_drv_register(&indevDrv);

  s_group = lv_group_create();
  lv_indev_set_group(indev, s_group);
}

lv_group_t* lv_port_group() { return s_group; }
