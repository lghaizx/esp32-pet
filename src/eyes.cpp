// =====================================================================
//  eyes.cpp  -  animated face / eye engine
// =====================================================================
#include "eyes.h"
#include "cnfont.h"
#include <math.h>

// Pixel size of the corner read-out's CJK labels (the ASCII digits keep their
// 6x8 size). 12 px reads clearly but sits quietly under the pet's eyebrows;
// drop to 10 for even smaller, raise back towards 16 for the old size.
#define HUD_GLYPH_PX 12

static const char* kMoodNames[MOOD_COUNT] = {
  "平静","开心","悲伤","生气","惊讶","困倦","爱意",
  "眩晕","生病","死亡","眨眼","好奇"
};
const char* moodName(EyeMood m) { return (m < MOOD_COUNT) ? kMoodNames[m] : "?"; }

// ---------------------------------------------------------------- setup
bool EyeEngine::begin(TFT_eSPI* tft) {
  _tft = tft;
  _spr.setColorDepth(16);
  _spr.setSwapBytes(SPRITE_SWAP_BYTES);
  if (_spr.createSprite(FACE_W, PET_H) == nullptr) { _ready = false; return false; }
  _ready = true;
  _t = 0; _nextBlink = 700; _nextGaze = 600;
  _mood = _base = MOOD_NEUTRAL;
  gradientBg();
#if !USE_LVGL
  _spr.pushSprite(0, 0);        // legacy UI paints the face itself
#endif
  return true;
}

uint16_t* EyeEngine::pixels() { return (uint16_t*)_spr.getPointer(); }

void EyeEngine::setBaseMood(EyeMood m) { _base = m; _mood = m; _moodUntil = 0; }
void EyeEngine::setMood(EyeMood m)     { _base = m; _mood = m; _moodUntil = 0; }
void EyeEngine::flashMood(EyeMood m, uint32_t ms) { _mood = m; _moodUntil = _t + ms; }

void EyeEngine::say(const char* text, uint32_t ms) { _say = text; _sayUntil = _t + ms; }

void EyeEngine::look(int8_t x, int8_t y) {
  _gx = x; _gy = y; _nextGaze = _t + 1500;
}
void EyeEngine::lookRandom() {
  _gx = (int8_t)random(-70, 71);
  _gy = (int8_t)random(-45, 46);
  _nextGaze = _t + random(900, 2600);
}
void EyeEngine::blinkNow() { _blinking = true; _blinkStart = _t; _blink = 0; }

// ---------------------------------------------------------- care animation
// See EyeAction in eyes.h / careaction.h. The overlay is drawn over the face, so
// it is the *cheapest* animation this project can have: it costs no LVGL widget,
// no LVGL pool byte and no extra buffer - just the frame's normal repaint.
void EyeEngine::playAction(EyeAction a, uint32_t ms) {
  _act      = a;
  _actStart = _t;
  _actMs    = ms ? ms : 1;
}

void EyeEngine::endAction() { _act = ACT_NONE; }

uint8_t EyeEngine::actionPct() const {
  if (_act == ACT_NONE) return 100;
  const uint32_t e = _t - _actStart;
  if (e >= _actMs) return 100;
  return (uint8_t)((e * 100UL) / _actMs);
}

// ---------------------------------------------------------------- update
void EyeEngine::update(uint32_t dt) {
  if (!_ready) return;
  _t += dt;

  if (_moodUntil && _t >= _moodUntil) { _mood = _base; _moodUntil = 0; }

  // a care animation also ends on its own, in case nobody calls endAction()
  if (_act != ACT_NONE && _t - _actStart >= _actMs) _act = ACT_NONE;

  // breathing bob
  _bob = (int)(sinf(_t / 620.0f) * 3.0f);

  // gaze wander
  if (_t >= _nextGaze) lookRandom();
  float k = 0.12f;
  _px += ((_gx / 100.0f * 12.0f) - _px) * k;
  _py += ((_gy / 100.0f * 9.0f)  - _py) * k;

  // blink animation
  if (_blinking) {
    uint32_t e = _t - _blinkStart;
    if (e >= 170) { _blinking = false; _blink = 0; _nextBlink = _t + random(1400, 4600); }
    else { float p = e / 170.0f; _blink = (p < 0.5f) ? p * 2.0f : (1.0f - p) * 2.0f; }
  } else if (_t >= _nextBlink && _mood != MOOD_SLEEPY) {
    _blinking = true; _blinkStart = _t; _blink = 0;
  }
}

// Redraw the sprite without pushing it anywhere: the LVGL build blits the
// buffer through its canvas widget instead.
void EyeEngine::draw() {
  if (!_ready) return;
  drawFace();
  if (_act != ACT_NONE) drawAction();         // care animation, over the face
  drawHud();                                  // scene corner read-outs
  if (_say && _t < _sayUntil) drawSpeech();   // bubble lives in the face sprite
}

void EyeEngine::render() {
  draw();
#if !USE_LVGL
  _spr.pushSprite(0, 0);
#endif
}

// ---------------------------------------------------------------- prims
static uint16_t gradColor(int y);      // scene background colour of row y

// Fill the whole scene: the pet's own gradient over the art area plus the
// continuing background below it (see gradColor()).
void EyeEngine::gradientBg() {
  for (int y = 0; y < PET_H; y++) _spr.drawFastHLine(0, y, FACE_W, gradColor(y));
}

void EyeEngine::hairline(int x0, int y0, int x1, int y1, int t, uint16_t c) {
  if (t < 1) t = 1;
  for (int i = -(t / 2); i <= t / 2; i++) _spr.drawLine(x0, y0 + i, x1, y1 + i, c);
}

void EyeEngine::curve(int cx, int cy, float r, float a0, float a1, int t, uint16_t c) {
  const int steps = 18;
  int px = 0, py = 0;
  for (int i = 0; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * i / steps) * DEG_TO_RAD;
    int x = (int)(cx + r * cosf(a));
    int y = (int)(cy + r * sinf(a));
    if (i > 0) hairline(px, py, x, y, t, c);
    px = x; py = y;
  }
}

// The face art is authored on a 240x170 grid; FSCX()/FSCY() scale it to the
// real FACE_W x FACE_H of the panel (keeps the art portable across sizes).
#define FSCX(v) ((int)((v) * (FACE_W / 240.0f) + 0.5f))
#define FSCY(v) ((int)((v) * (FACE_H / 170.0f) + 0.5f))

void EyeEngine::eyeBall(int cx, int cy, float open, float px, float py,
                        uint16_t iris, float scale, float pupilScale) {
  int rxs = FSCX((int)(40 * scale));
  int rys = FSCY((int)(46 * scale));
  int ryv = (int)(rys * open);
  if (ryv < 3) ryv = 3;
  _spr.fillEllipse(cx, cy, rxs + 2, ryv + 2, COL_OUTLINE);
  _spr.fillEllipse(cx, cy, rxs, ryv, COL_SCLERA);
  if (open > 0.30f) {
    int ir  = FSCX((int)(24 * scale));
    int iry = (ir > ryv - 3) ? ryv - 3 : ir;
    if (iry < 2) iry = 2;
    _spr.fillEllipse(cx + (int)px, cy + (int)py, ir, iry, iris);
    int pr = FSCX((int)(11 * pupilScale));
    if (pr > iry) pr = iry;
    if (pr < 1) pr = 1;
    _spr.fillCircle(cx + (int)px, cy + (int)py, pr, COL_PUPIL);
    _spr.fillCircle(cx + (int)px - FSCX(8), cy + (int)py - FSCY(9), FSCX(5), COL_SCLERA);
    _spr.fillCircle(cx + (int)px + FSCX(7), cy + (int)py + FSCY(7), FSCX(3), COL_SCLERA);
  }
}

void EyeEngine::heart(int cx, int cy, int sIn, uint16_t c) {
  int s = FSCX(sIn);
  if (s < 3) s = 3;
  _spr.fillCircle(cx - s / 2, cy - s / 3, s / 2 + 2, c);
  _spr.fillCircle(cx + s / 2, cy - s / 3, s / 2 + 2, c);
  _spr.fillTriangle(cx - s - 2, cy - s / 6, cx + s + 2, cy - s / 6, cx, cy + s, c);
}

void EyeEngine::cross(int cx, int cy, int sIn, uint16_t c) {
  int s = FSCX(sIn);
  int t = FSCX(6); if (t < 2) t = 2;
  hairline(cx - s, cy - s, cx + s, cy + s, t, c);
  hairline(cx - s, cy + s, cx + s, cy - s, t, c);
}

void EyeEngine::mouthSmile(int mx, int my, int wIn, uint16_t c, bool up) {
  int w = FSCX(wIn);
  int t = FSCX(5); if (t < 2) t = 2;
  if (up) curve(mx, my - w / 2, (float)w, 40, 140, t, c);   // smile  (U)
  else    curve(mx, my + w / 2, (float)w, 220, 320, t, c);   // frown  (n)
}

// Background colour of scene row y (also used to "cut out" shapes, so it must
// stay in sync with gradientBg()). Rows 0..FACE_H-1 are the pet's own art
// background; below that the gradient continues towards a lighter "ground glow"
// so the status HUD sitting there stays readable.
static uint16_t gradColor(int y) {
  if (y < 0) y = 0; if (y > PET_H - 1) y = PET_H - 1;
  float f;
  if (y < FACE_H) f = (float)y / (FACE_H - 1);
  else            f = 1.0f + 0.55f * (float)(y - FACE_H) / (PET_H - FACE_H);
  uint8_t r = 14 + (uint8_t)((32 - 14) * f);
  uint8_t g = 20 + (uint8_t)((40 - 20) * f);
  uint8_t b = 40 + (uint8_t)((78 - 40) * f);
  return RGB565(r, g, b);
}

// ---------------------------------------------------------------- face
void EyeEngine::drawFace() {
  gradientBg();

  const int cx  = FACE_W / 2;
  const int ey  = FSCY(84) + _bob;
  const int lx  = FSCX(72), rx = FSCX(168);
  const int my  = FSCY(140) + _bob;
  const int bow = FSCX(30);          // eyebrow half-width
  float open = 1.0f - _blink;
  float px = _px, py = _py;

  switch (_mood) {

    case MOOD_HAPPY:
      curve(lx, ey - 2, FSCX(40), 205, 335, FSCX(7), COL_HAPPY);
      curve(rx, ey - 2, FSCX(40), 205, 335, FSCX(7), COL_HAPPY);
      mouthSmile(cx, my - FSCY(6), 30, COL_MOUTH, true);
      break;

    case MOOD_LOVE: {
      float pulse = 1.0f + 0.12f * sinf(_t / 180.0f);
      int s = (int)(20 * pulse);
      heart(lx, ey, s, COL_LOVE);
      heart(rx, ey, s, COL_LOVE);
      mouthSmile(cx, my - FSCY(6), 30, COL_MOUTH, true);
      break; }

    case MOOD_SAD:
      eyeBall(lx, ey, open, px, py + FSCY(8), COL_IRIS, 1.0f, 1.0f);
      eyeBall(rx, ey, open, px, py + FSCY(8), COL_IRIS, 1.0f, 1.0f);
      hairline(lx - bow, ey - FSCY(56), lx + FSCX(28), ey - FSCY(70), FSCX(6), COL_BROW);
      hairline(rx + bow, ey - FSCY(56), rx - FSCX(28), ey - FSCY(70), FSCX(6), COL_BROW);
      mouthSmile(cx, my + FSCY(6), 28, COL_MOUTH, false);
      break;

    case MOOD_ANGRY: {
      float o = open * 0.65f;
      eyeBall(lx, ey + FSCY(4), o, px, py, COL_ANGRY, 1.0f, 1.0f);
      eyeBall(rx, ey + FSCY(4), o, px, py, COL_ANGRY, 1.0f, 1.0f);
      hairline(lx - bow, ey - FSCY(70), lx + FSCX(28), ey - FSCY(54), FSCX(7), COL_ANGRY);
      hairline(rx + bow, ey - FSCY(70), rx - FSCX(28), ey - FSCY(54), FSCX(7), COL_ANGRY);
      mouthSmile(cx, my + FSCY(8), 26, COL_MOUTH, false);
      break; }

    case MOOD_SURPRISED:
      eyeBall(lx, ey, open, px, py, COL_IRIS, 1.15f, 0.5f);
      eyeBall(rx, ey, open, px, py, COL_IRIS, 1.15f, 0.5f);
      _spr.fillEllipse(cx, my, FSCX(11), FSCY(14), COL_MOUTH);
      _spr.fillEllipse(cx, my, FSCX(6), FSCY(9), gradColor(my));
      break;

    case MOOD_SLEEPY: {
      float o = 0.5f - 0.35f * _blink;
      if (o < 0.18f) o = 0.18f;
      eyeBall(lx, ey + FSCY(6), o, px, py, COL_IRIS2, 1.0f, 1.05f);
      eyeBall(rx, ey + FSCY(6), o, px, py, COL_IRIS2, 1.0f, 1.05f);
      hairline(lx - FSCX(34), ey - FSCY(30), lx + FSCX(34), ey - FSCY(30), FSCX(6), COL_BROW);
      hairline(rx - FSCX(34), ey - FSCY(30), rx + FSCX(34), ey - FSCY(30), FSCX(6), COL_BROW);
      _spr.fillEllipse(cx, my, FSCX(7), FSCY(9), COL_MOUTH);
      break; }

    case MOOD_DIZZY: {
      cross(lx, ey, 30, COL_HAPPY);
      cross(rx, ey, 30, COL_HAPPY);
      int zs  = FSCX(16);
      int zx0 = cx - FSCX(32), zx1 = cx + FSCX(32);
      int prevY = my + FSCY(6);
      for (int x = zx0; x < zx1; x += zs) {
        int ny = my + ((x / zs) & 1 ? -FSCY(6) : FSCY(6));
        hairline(x, prevY, x + zs, ny, FSCX(4), COL_MOUTH);
        prevY = ny;
      }
      break; }

    case MOOD_SICK: {
      float o = open * 0.85f;
      eyeBall(lx, ey, o, px, py + FSCY(4), COL_SICK, 1.0f, 0.9f);
      eyeBall(rx, ey, o, px, py + FSCY(4), COL_SICK, 1.0f, 0.9f);
      hairline(lx - FSCX(28), ey - FSCY(66), lx + FSCX(26), ey - FSCY(60), FSCX(5), COL_SICK);
      hairline(rx + FSCX(28), ey - FSCY(66), rx - FSCX(26), ey - FSCY(60), FSCX(5), COL_SICK);
      int zs  = FSCX(12);
      int zx0 = cx - FSCX(28), zx1 = cx + FSCX(28);
      int prevY = my - FSCY(5);
      for (int x = zx0; x < zx1; x += zs) {
        int ny = my + ((x / zs) & 1 ? FSCY(5) : -FSCY(5));
        hairline(x, prevY, x + zs, ny, FSCX(4), COL_MOUTH);
        prevY = ny;
      }
      break; }

    case MOOD_DEAD:
      cross(lx, ey, 30, COL_DEAD);
      cross(rx, ey, 30, COL_DEAD);
      hairline(cx - FSCX(18), my, cx + FSCX(18), my, FSCX(5), COL_DEAD);
      break;

    case MOOD_WINK:
      eyeBall(lx, ey, open, px, py, COL_IRIS, 1.0f, 1.0f);
      curve(rx, ey - 2, FSCX(38), 205, 335, FSCX(7), COL_HAPPY);
      mouthSmile(cx, my - FSCY(6), 28, COL_MOUTH, true);
      break;

    case MOOD_CURIOUS:
      eyeBall(lx, ey, open, px, py, COL_IRIS, 1.0f, 1.0f);
      eyeBall(rx, ey, open, px, py, COL_IRIS, 1.0f, 1.0f);
      hairline(lx - bow, ey - FSCY(60), lx + FSCX(28), ey - FSCY(60), FSCX(6), COL_BROW);
      hairline(rx - FSCX(28), ey - FSCY(78), rx + bow, ey - FSCY(66), FSCX(6), COL_BROW);
      mouthSmile(cx, my - FSCY(4), 22, COL_MOUTH, true);
      break;

    default: // MOOD_NEUTRAL
      eyeBall(lx, ey, open, px, py, COL_IRIS, 1.0f, 1.0f);
      eyeBall(rx, ey, open, px, py, COL_IRIS, 1.0f, 1.0f);
      hairline(lx - FSCX(28), ey - FSCY(60), lx + FSCX(26), ey - FSCY(62), FSCX(5), COL_BROW);
      hairline(rx + FSCX(28), ey - FSCY(60), rx - FSCX(26), ey - FSCY(62), FSCX(5), COL_BROW);
      mouthSmile(cx, my - FSCY(2), 22, COL_MOUTH, true);
      break;
  }
}

// ------------------------------------------------------------ care action
// The "it does something" beat of a care action (see careaction.h): a berry for
// feeding, bubbles for washing, a pill and its bitter shockwave for the medicine.
// It is laid out on the same 240x170 art grid as drawFace() (FSCX/FSCY) and stops
// at FACE_H - the rows below that belong to the status HUD.
void EyeEngine::drawAction() {
  if (_act == ACT_NONE) return;

  const float    PI_F  = 3.14159265f;
  const int      cx    = FACE_W / 2;
  const int      my    = FSCY(140) + _bob;        // mouth centre (see drawFace)
  const float    p     = actionPct() / 100.0f;    // progress 0..1
  const uint16_t SHINE = RGB565(238, 250, 255);   // berry / bubble highlight

  switch (_act) {

    case ACT_EAT: {
      // first half: the berry flies in from the right; second half: chewing - the
      // mouth opens and closes while a few crumbs fly off
      const int bxc = cx + FSCX(6), by = my - FSCY(6);
      if (p < 0.45f) {
        const float q = p / 0.45f;
        const int   x = (int)((FACE_W + FSCX(12)) + (bxc - (FACE_W + FSCX(12))) * q);
        const int   r = FSCX(11);
        _spr.fillCircle(x, by, r, COL_LOVE);
        _spr.fillCircle(x - r / 2, by - r / 2, r / 3 ? r / 3 : 1, SHINE);
        hairline(x + 1, by - r, x + FSCX(4), by - r - FSCY(8), FSCX(4), COL_GOOD);
      } else {
        const float q  = (p - 0.45f) / 0.55f;
        const int   ry = FSCY(3) + (int)(fabsf(sinf(q * 22.0f)) * FSCY(5));
        _spr.fillEllipse(bxc, by + FSCY(4), FSCX(10), ry, COL_OUTLINE);
        for (int i = 0; i < 3; i++) {                          // crumbs
          const int d = (int)(FSCX(10) + FSCX(30) * q);
          const int r = (int)(FSCX(5) * (1.0f - q));
          if (r < 1) continue;
          const float a = (195.0f + i * 45.0f) * DEG_TO_RAD;
          _spr.fillCircle(bxc + (int)(d * cosf(a)), by + (int)(d * sinf(a) * 0.6f),
                          r, COL_WARN);
        }
      }
      break;
    }

    case ACT_WASH: {
      // six bubbles climb the two side margins (the eyes fill the middle of the
      // face), each with its own phase, wobble and "pop" at the top
      for (int i = 0; i < 6; i++) {
        float q = p + i / 6.0f;
        q -= (int)q;                                           // this bubble 0..1
        const int r = (int)(FSCX(10) * sinf(q * PI_F)) - 1;
        if (r < 2) continue;
        const int x = (i & 1 ? FSCX(26) : FSCX(214)) +
                      (int)(sinf((q + i) * 6.5f) * FSCX(7));
        const int y = (int)((my + FSCY(10)) + (FSCY(10) - (my + FSCY(10))) * q);
        _spr.fillCircle(x, y, r, COL_ACCENT);
        _spr.fillCircle(x, y, r > 2 ? r - 2 : 1, SHINE);
        if (r > 3) _spr.fillCircle(x - r / 2, y - r / 2, r / 4 ? r / 4 : 1, COL_SCLERA);
      }
      break;
    }

    case ACT_MEDICINE: {
      // first half: the pill flies in from the left; second half: a ring spreads
      // around the mouth while two droplets leave the cheek ("bitter!")
      const int bxc = cx - FSCX(6), by = my - FSCY(6);
      if (p < 0.45f) {
        const float q  = p / 0.45f;
        const int   x  = (int)((-FSCX(12)) + (bxc + FSCX(12)) * q);
        const int   rx = FSCX(12), ry = FSCY(8) > 2 ? FSCY(8) : 2;
        _spr.fillEllipse(x, by, rx, ry, COL_SCLERA);
        _spr.fillEllipse(x - rx / 2, by, rx / 2, ry, COL_WARN);
        _spr.drawEllipse(x, by, rx, ry, COL_OUTLINE);
      } else {
        const float q  = (p - 0.45f) / 0.55f;
        const int   rr = (int)(FSCX(12) + FSCX(24) * q);
        _spr.drawEllipse(cx, my - FSCY(2), rr, rr / 2, COL_HAPPY);
        for (int i = 0; i < 2; i++) {
          const int r = (int)(FSCX(5) * (1.0f - q));
          if (r < 1) continue;
          _spr.fillEllipse(FACE_W - FSCX(14) - i * FSCX(8), my - FSCY(6) + i * FSCY(8),
                           r, r + r / 2, COL_HAPPY);
        }
      }
      break;
    }

    default: break;
  }
}

// ---------------------------------------------------------------- speech
// A rounded bubble with a little tail, centred at the top of the face. It is
// drawn into the face sprite itself so it appears / disappears without any
// flicker and never disturbs the animation loop.
void EyeEngine::drawSpeech() {
  if (!_say) return;

  const uint16_t fill = RGB565(244, 248, 255);
  const uint16_t edge = RGB565(30, 120, 220);
  const uint16_t txt  = RGB565(12, 16, 34);

  int tw = cnStrW(_say);
  int bw = tw + 12;
  if (bw > FACE_W - 4) bw = FACE_W - 4;
  int bx = (FACE_W - bw) / 2;
  int by = 2;
  int bh = CN_GLYPH_H + 6;

  _spr.fillRoundRect(bx, by, bw, bh, 5, fill);
  _spr.drawRoundRect(bx, by, bw, bh, 5, edge);

  int tx = bx + bw / 2;                        // small tail pointing down
  _spr.fillTriangle(tx - 5, by + bh - 1, tx + 5, by + bh - 1, tx, by + bh + 5, fill);
  _spr.drawLine(tx - 5, by + bh - 1, tx, by + bh + 5, edge);
  _spr.drawLine(tx + 5, by + bh - 1, tx, by + bh + 5, edge);

  cnDrawC(&_spr, bx + bw / 2, by + 3, _say, txt, fill);
}

// ------------------------------------------------------------- sensor HUD
// Light (top left) and temperature (top right), painted straight into the scene
// sprite: the read-out is therefore part of the picture and shows up on every
// screen that displays the face canvas, without a single LVGL widget.
//
// cnfont's "over" plotter touches only the glyph pixels (the background keeps
// showing through) and a 1 px black copy underneath acts as a drop shadow, so
// the text stays readable even where the pet's eyebrows sit behind it.
void EyeEngine::setHud(const char* left, const char* right) {
  if (left) { strncpy(_hudL, left, sizeof(_hudL) - 1); _hudL[sizeof(_hudL) - 1] = 0; }
  else        _hudL[0] = 0;
  if (right){ strncpy(_hudR, right, sizeof(_hudR) - 1); _hudR[sizeof(_hudR) - 1] = 0; }
  else        _hudR[0] = 0;
}

void EyeEngine::drawHud() {
  const uint16_t shadow = RGB565(0, 0, 0);
  const uint16_t ink    = COL_ACCENT;
  if (_hudL[0]) {
    cnDrawOverSm (&_spr, 3,      1, _hudL, shadow, HUD_GLYPH_PX);
    cnDrawOverSm (&_spr, 2,      0, _hudL, ink,    HUD_GLYPH_PX);
  }
  if (_hudR[0]) {
    cnDrawOverSmR(&_spr, FACE_W - 3, 1, _hudR, shadow, HUD_GLYPH_PX);
    cnDrawOverSmR(&_spr, FACE_W - 2, 0, _hudR, ink,    HUD_GLYPH_PX);
  }
}
