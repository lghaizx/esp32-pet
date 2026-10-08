// =====================================================================
//  lv_conf.h  -  minimal LVGL v8 configuration for the ESP32 Pixel Pet
//
//  LVGL's lv_conf_internal.h supplies a default for every option, so we
//  only override the handful that matter here. The file is picked up via
//  -DLV_CONF_INCLUDE_SIMPLE (see platformio.ini).
// =====================================================================
#pragma once
#define LV_CONF_H        // marks the config as successfully included

/* -------- colour -------- */
#define LV_COLOR_DEPTH     16
// The ST7735 is a big-endian 16 bit panel: LVGL builds its draw buffer in
// swapped byte order and the driver therefore disables TFT_eSPI's own byte
// swapping (see lv_port.cpp). Both must agree, otherwise red and blue swap.
#define LV_COLOR_16_SWAP   1

/* -------- memory (LVGL's own TLSF allocator over a static pool) -------- */
// Every widget (12 screens, labels, bars, menu buttons) and every per-object
// style copy lives in this pool, which is a plain static array (.dram0 on the
// ESP32 - see lv_port.cpp for why the draw buffer was moved off it); the
// face/panel sprites do not live here, they are owned by EyeEngine. The pool is
// sized from the boot log: setup() prints "[ui] LVGL pool ..." right after the
// screens are built, so this number can be checked instead of guessed (see
// README "LVGL 内存").
// Measured on this UI: 49152 B ran at 99% (704 B free) - one widget too many and
// the next allocation fails - hence 64 KB, which leaves ~13 KB of headroom for
// the gradient cache, talk bubbles and future pages.
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (64U * 1024U)

// Gradient maps (used by the card / header / chip backgrounds) get their own
// cache. With the LVGL default of 0 every gradient is malloc'd from the pool
// above for every single draw call, and when that allocation fails LVGL 8.4
// dereferences the NULL gradient (lv_draw_sw_rect.c: draw_bg) and panics - so
// this cache is not an optimisation here, it is what keeps the UI alive on a
// busy screen. 4 KB holds several full width (160 px) maps.
#define LV_GRAD_CACHE_DEF_SIZE (4U * 1024U)

/* -------- tick source: Arduino millis() -------- */
#define LV_TICK_CUSTOM      1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* -------- trim debug / monitoring -------- */
#define LV_USE_LOG          0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* -------- widgets we use -------- */
#define LV_USE_LABEL   1
#define LV_USE_BTN     1
#define LV_USE_BAR     1
#define LV_USE_LIST    1
#define LV_USE_LINE    1
#define LV_USE_IMG     1
#define LV_USE_CANVAS  1
#define LV_USE_LED     1

/* -------- fonts -------- */
// The type scale is three sizes (mirroring UI_TXT_* in uistyle.h): 14 = digits
// and latin inside a table row, 20 = the single big number of a page; words
// with CJK characters come from the project font lv_font_cn, never from LVGL.
// Each montserrat size costs ~16 KB of flash, so only the used ones are on.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
