// =====================================================================
//  splash.h  -  the boot picture ("开机图片")
//
//  splash_map[] is a full screen bitmap baked into the firmware, produced by
//  tools/gen_splash.ps1 (from tools/splash.png, or from the built-in artwork
//  when no file is there). Both setup()s push it through TFT_eSPI right after
//  tft.init() and before LVGL starts, and the UI's first flush then paints the
//  welcome screen over it, so powering up reads
//
//      picture  ->  welcome screen (cfg.bootTitle + greeting)  ->  pet
//
//  A picture uploaded from the settings page (USE_SPLASH_UPLOAD, see config.h)
//  is preferred over the baked one. The two live in very different places -
//  /splash.bin in the "spiffs" flash partition vs. splash_map[] inside the app
//  - but are byte for byte the same thing: 160x128 plain RGB565, row major. One
//  strip reader therefore serves both (see splash.cpp).
//
//  Replace the built-in picture with your own:
//      copy /path/to/logo.png tools\splash.png
//      powershell -ExecutionPolicy Bypass -File tools/gen_splash.ps1
//  then rebuild. USE_SPLASH 0 in config.h removes it (and its 40 KB) again.
// =====================================================================
#pragma once
#include <TFT_eSPI.h>
#include "config.h"

// Geometry of the baked bitmap; must equal SCR_W / SCR_H of the panel's
// landscape layout (src/splash_data.cpp asserts that at compile time).
#define SPLASH_W 160
#define SPLASH_H 128

// Plain RGB565 (red in the high bits), row major. The panel wants the high
// byte first; pushImage() is told to swap (see splash.cpp).
extern const uint16_t splash_map[SPLASH_W * SPLASH_H];

// Paint the picture and hold it for SPLASH_MS; A or B cuts it short. A no-op
// with USE_SPLASH 0, so both setup()s may call it unconditionally. It prefers
// an uploaded picture over the built-in one and settles which of the two ended
// up on the panel before it returns.
void splashShow(TFT_eSPI* d);

// ------------------------------------------------------- the uploaded picture
// Where the settings page keeps what it uploaded: SPLASH_W * SPLASH_H * 2 bytes
// of raw RGB565, i.e. exactly the layout splash_map[] above has.
#define SPLASH_FILE     "/splash.bin"
#define SPLASH_FILE_TMP "/splash.tmp"
#define SPLASH_FILE_MAX ((size_t)SPLASH_W * SPLASH_H * sizeof(uint16_t))

#if USE_SPLASH && USE_SPLASH_UPLOAD
// The file system on the "spiffs" partition, mounted on demand and dropped
// again (splashShow() mounts it for the one draw it needs). Nothing else in the
// pet ever looks at that partition: it starts out unformatted and LittleFS
// formats it the first time it is needed.
bool   splashStoreBegin();       // true once the store can be used
void   splashStoreEnd();         // release it (the picture stays in flash)
bool   splashStoreHasImage();    // a complete picture is stored
size_t splashStoreImageSize();   // bytes in SPLASH_FILE (0 when there is none)
bool   splashStoreDelete();      // back to the built-in picture

// Upload, piece by piece, exactly as the web server's multipart parser hands
// them over (see netconfig.cpp): start -> chunk* -> finish, or abort halfway.
bool   splashUploadStart();
bool   splashUploadChunk(const uint8_t* data, size_t len);
bool   splashUploadFinish();     // only a complete picture is accepted
void   splashUploadAbort();      // also drops a half written file
#endif
