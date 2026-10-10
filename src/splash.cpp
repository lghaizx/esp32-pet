// =====================================================================
//  splash.cpp  -  the boot picture, shared by both UIs
//
//  The LVGL build (app_lvgl.cpp) and the legacy TFT_eSPI build (main.cpp)
//  call splashShow() at the very same point - right after tft.init(), before
//  the UI takes the panel - so the boot looks identical whichever one is
//  compiled in. Same trick as careaction.h: one implementation, two redraws.
//
//  The picture can come from two places (see splash.h): an uploaded file in the
//  "spiffs" flash partition, which wins when it is there, or the baked-in
//  splash_map[] in the app. Both are the same bytes (160x128 RGB565, row major),
//  so they differ only in how a strip gets into the line buffer.
// =====================================================================
#include "config.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "buttons.h"
#include "splash.h"

#if USE_SPLASH && USE_SPLASH_UPLOAD
#include <LittleFS.h>
#endif

#if USE_SPLASH

// One strip of the picture at a time: 160*16*2 = 5120 bytes. A whole frame
// would be 40 KB of RAM held all boot long for a picture drawn exactly once.
static uint16_t s_strip[SPLASH_W * SPLASH_STRIP_ROWS];

static void pushStrip(TFT_eSPI* d, int y, int rows) {
  d->pushImage(0, y, SPLASH_W, rows, s_strip);
}

// The baked-in bitmap lives in flash, so a strip is a copy - the strip reader
// then takes over the byte swap just like it does for an uploaded picture.
static void drawFromFlash(TFT_eSPI* d) {
  for (int y = 0; y < SPLASH_H; y += SPLASH_STRIP_ROWS) {
    const int    rows = min((int)SPLASH_STRIP_ROWS, (int)SPLASH_H - y);
    const size_t n    = (size_t)rows * SPLASH_W * sizeof(uint16_t);
    memcpy(s_strip, splash_map + (size_t)y * SPLASH_W, n);
    pushStrip(d, y, rows);
  }
}

#if USE_SPLASH_UPLOAD
// Same, straight out of the file system. False when the file turns out to be
// shorter than a picture - the caller falls back to the built-in one.
static bool drawFromFile(TFT_eSPI* d, File& f) {
  for (int y = 0; y < SPLASH_H; y += SPLASH_STRIP_ROWS) {
    const int    rows = min((int)SPLASH_STRIP_ROWS, (int)SPLASH_H - y);
    const size_t n    = (size_t)rows * SPLASH_W * sizeof(uint16_t);
    if (f.read((uint8_t*)s_strip, n) != n) return false;
    pushStrip(d, y, rows);
  }
  return true;
}
#endif

#endif  // USE_SPLASH

void splashShow(TFT_eSPI* d) {
#if USE_SPLASH
  // setup() has just cleared the panel, so this is the first thing on it. The
  // bitmap is plain RGB565 while the ST7735 is big endian, so TFT_eSPI has to
  // swap the two bytes of every pixel. That flag is the same one lv_port_init()
  // clears again for LVGL (which swaps its own draw buffer - see lv_conf.h),
  // hence the explicit restore.
  d->setSwapBytes(true);

  // An uploaded picture beats the one in the app. Mounting the file system is
  // free of side effects (nothing else uses that partition) and it is dropped
  // again right below - the picture only has to reach the panel once.
  bool drawn = false;
#if USE_SPLASH_UPLOAD
  if (splashStoreHasImage()) {
    File f = LittleFS.open(SPLASH_FILE, "r");
    if (f) {
      drawn = drawFromFile(d, f);
      if (!drawn) Serial.println("[splash] uploaded picture is short - using the built-in one");
      f.close();
    }
  }
  splashStoreEnd();
#endif
  const char* from = "built-in picture";
  if (drawn) from = "uploaded picture";
  else       drawFromFlash(d);

  d->setSwapBytes(false);
  Serial.printf("[ui] splash %dx%d, %s, up to %u ms (A/B skips)\n",
                (int)SPLASH_W, (int)SPLASH_H, from, (unsigned)SPLASH_MS);

  // Hold the picture. The keys are initialised by now and the frame loop has
  // not started yet, so this small poll is the only way the wait can be cut
  // short - which is the case that matters: nobody wants to sit through a
  // logo after a power cut.
  const uint32_t t0 = millis();
  while (millis() - t0 < SPLASH_MS) {
    buttonsUpdate();
    if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I)) break;
    delay(10);
  }
  // Drop the latched edges of this wait: a key kept down over the picture would
  // otherwise read as a fresh press on the very first frame of the welcome
  // screen (which uses A / B to skip) and swallow it.
  buttonsEndFrame();
#else
  (void)d;                 // no picture: boot straight into the UI
#endif
}

// =====================================================================
//  the uploaded picture - the file system half
//
//  The bytes land in the flash partition the default partition table already
//  reserves for a file system ("spiffs", 1.4 MB at 0x290000 - `pio run -t
//  uploadfs` writes to the same place). It is dead weight on a pet that never
//  uploads anything, so LittleFS is only mounted when a picture is drawn or
//  uploaded, and unmounted again right after: the pet never keeps a file
//  system open while it is just being a pet.
// =====================================================================
#if USE_SPLASH && USE_SPLASH_UPLOAD

static bool   s_fsReady = false;     // mount state of the store
static bool   s_upOpen  = false;     // an upload is being written
static size_t s_upBytes = 0;
static File   s_upFile;

bool splashStoreBegin() {
  if (s_fsReady) return true;
  // formatOnFail: a pet that never uploaded anything has an unformatted area
  // here, so the very first upload formats it - and only ever that partition.
  s_fsReady = LittleFS.begin(true, "/littlefs", 8, "spiffs");
  if (!s_fsReady) Serial.println("[splash] no file system - uploads are off");
  return s_fsReady;
}

void splashStoreEnd() {
  if (!s_fsReady) return;
  LittleFS.end();
  s_fsReady = false;
}

// Size of one entry in the root directory (which holds at most the picture and a
// half written temporary file), 0 when it is not there. Listing the directory
// instead of asking for the file is deliberate: VFSImpl::open() logs an error
// for a file that does not exist, and "not there" is the normal state for both
// of them - a pet without an uploaded picture would log a fault on every boot.
static size_t fileSize(const char* name) {
  if (!splashStoreBegin()) return 0;
  File root = LittleFS.open("/");
  if (!root) return 0;
  size_t n = 0;
  // openNextFile() spells an entry the way it sits in the directory ("splash.bin",
  // no leading slash) while the constants in splash.h are spelled the way open()
  // wants them ("/splash.bin"), so compare the last path element - and require it
  // to end there, so the picture never matches a "splash.bin.old".
  const char*  base = (name[0] == '/') ? name + 1 : name;
  const size_t len  = strlen(base);
  for (File e = root.openNextFile(); e; e = root.openNextFile()) {
    const char* p = strstr(e.name(), base);
    if (p && p[len] == 0 && (p == e.name() || p[-1] == '/')) { n = e.size(); break; }
  }
  root.close();
  return n;
}

size_t splashStoreImageSize() { return fileSize(SPLASH_FILE); }

// Anything that is not exactly one picture counts as "no picture": a torn file
// after a power cut must not end up half drawn on the panel.
bool splashStoreHasImage() { return fileSize(SPLASH_FILE) == SPLASH_FILE_MAX; }

bool splashStoreDelete() {
  if (!splashStoreBegin()) return false;
  const bool ok = !fileSize(SPLASH_FILE) || LittleFS.remove(SPLASH_FILE);
  Serial.printf("[splash] uploaded picture %s\n",
                ok ? "deleted - the built-in picture is back" : "could NOT be deleted");
  return ok;
}

// ---- upload -------------------------------------------------------------
bool splashUploadStart() {
  splashUploadAbort();                       // whatever an earlier attempt left
  if (!splashStoreBegin()) return false;
  s_upFile  = LittleFS.open(SPLASH_FILE_TMP, "w");   // writes from the start
  s_upOpen  = (bool)s_upFile;
  s_upBytes = 0;
  if (!s_upOpen) Serial.println("[splash] cannot create the temporary file");
  return s_upOpen;
}

bool splashUploadChunk(const uint8_t* data, size_t len) {
  // One picture and not a byte more: an oversized upload is a mistake (or a
  // stranger on the access point) and must not be able to fill the partition.
  if (!s_upOpen || s_upBytes + len > SPLASH_FILE_MAX) return false;
  if (s_upFile.write(data, len) != len) return false;
  s_upBytes += len;
  return true;
}

bool splashUploadFinish() {
  if (!s_upOpen) return false;
  s_upFile.close();
  s_upOpen = false;

  // Half a picture is worth nothing - refuse it instead of booting into noise.
  if (s_upBytes != SPLASH_FILE_MAX) {
    Serial.printf("[splash] upload refused: %u of %u bytes\n",
                  (unsigned)s_upBytes, (unsigned)SPLASH_FILE_MAX);
    LittleFS.remove(SPLASH_FILE_TMP);
    return false;
  }
  // Move it in place: remove-then-rename, because the old name has to be free.
  // A power cut in between leaves no picture at all, which is the state the
  // built-in one is for.
  LittleFS.remove(SPLASH_FILE);
  const bool ok = LittleFS.rename(SPLASH_FILE_TMP, SPLASH_FILE);
  Serial.printf("[splash] upload %s: %u bytes\n",
                ok ? "saved (used at the next boot)" : "FAILED to save", (unsigned)s_upBytes);
  return ok;
}

void splashUploadAbort() {
  if (s_upOpen) { s_upFile.close(); s_upOpen = false; }
  if (!s_fsReady) return;
  if (fileSize(SPLASH_FILE_TMP)) LittleFS.remove(SPLASH_FILE_TMP);
  s_upBytes = 0;
}

#endif  // USE_SPLASH && USE_SPLASH_UPLOAD

