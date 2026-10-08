// =====================================================================
//  eyes.h  -  animated eye/face engine (the "pet face")
// =====================================================================
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

enum EyeMood : uint8_t {
  MOOD_NEUTRAL = 0,
  MOOD_HAPPY,
  MOOD_SAD,
  MOOD_ANGRY,
  MOOD_SURPRISED,
  MOOD_SLEEPY,
  MOOD_LOVE,
  MOOD_DIZZY,
  MOOD_SICK,
  MOOD_DEAD,
  MOOD_WINK,
  MOOD_CURIOUS,
  MOOD_COUNT
};

const char* moodName(EyeMood m);

// A short care animation, drawn *over* the face (see drawAction() and
// careaction.h): the pet is shown doing something before the stat it produces
// exists. The overlay lives in the face sprite, so it needs no LVGL widget and
// therefore no memory from LVGL's own pool.
enum EyeAction : uint8_t {
  ACT_NONE = 0,
  ACT_EAT,        // a berry flies in from the right and is chewed
  ACT_WASH,       // bubbles rise past both cheeks
  ACT_MEDICINE    // a pill flies in from the left, then the shockwave
};

class EyeEngine {
public:
  explicit EyeEngine(TFT_eSPI* tft) : _tft(tft), _spr(tft) {}

  bool     begin(TFT_eSPI* tft);
  bool     ready() const { return _ready; }

  // Raw RGB565 buffer of the face sprite. LVGL shows it through an lv_canvas
  // widget (SPRITE_SWAP_BYTES keeps the pixels little-endian for LVGL).
  uint16_t* pixels();

  void     setBaseMood(EyeMood m);            // default expression
  void     setMood(EyeMood m);               // base == current (no timeout)
  void     flashMood(EyeMood m, uint32_t ms); // temporary expression
  EyeMood  mood() const { return _mood; }

  void     look(int8_t x, int8_t y);          // gaze bias -100..100
  void     lookRandom();                      // pick a new gaze point
  void     blinkNow();                        // force a blink

  // Care animation overlay (see EyeAction above). playAction() starts it, the
  // animation ends by itself after ms; the owner calls endAction() at the moment
  // it applies the stat, so "animation over" and "value changed" can never drift
  // apart (a screen change in between would otherwise leave it running).
  void     playAction(EyeAction a, uint32_t ms);
  void     endAction();
  bool     acting() const { return _act != ACT_NONE; }
  uint8_t  actionPct() const;                 // 0..100 progress of the action

  // show a short speech bubble over the face for ms milliseconds (bubble is
  // part of the face sprite, so it never flickers while it is on screen).
  void     say(const char* text, uint32_t ms);
  bool     saying() const { return _say && _t < _sayUntil; }

  // Small sensor read-out in the two top corners of the scene (light left,
  // temperature right). The strings are copied, so a temporary buffer is fine;
  // "" hides that side. They are painted into the sprite, i.e. they belong to
  // the scene and therefore show up on every screen that displays the face.
  void     setHud(const char* left, const char* right);

  void     update(uint32_t dtMs);             // animate (call every frame)
  void     draw();                            // redraw the sprite, no push
  void     render();                          // draw + push to TFT (legacy UI)

private:
  TFT_eSPI*   _tft = nullptr;
  TFT_eSprite _spr;
  bool        _ready = false;

  EyeMood     _mood = MOOD_NEUTRAL;
  EyeMood     _base = MOOD_NEUTRAL;
  uint32_t    _moodUntil = 0;

  uint32_t    _t = 0, _nextBlink = 700, _nextGaze = 600;
  bool        _blinking = false;
  uint32_t    _blinkStart = 0;
  float       _blink = 0;                 // 0 open .. 1 closed

  int8_t      _gx = 0, _gy = 0;           // target gaze  (-100..100)
  float       _px = 0, _py = 0;           // smoothed gaze (pixels)
  int16_t     _bob = 0;                   // breathing offset (pixels)

  const char* _say = nullptr;             // active speech bubble text (or null)
  uint32_t    _sayUntil = 0;              // bubble hides once _t >= this

  EyeAction   _act = ACT_NONE;            // running care animation (or none)
  uint32_t    _actStart = 0, _actMs = 1;  // its window, in EyeEngine's own clock

  char        _hudL[20] = { 0 };          // scene corner read-outs (light / temp)
  char        _hudR[20] = { 0 };

  void  gradientBg();
  void  hairline(int x0, int y0, int x1, int y1, int t, uint16_t c);
  void  curve(int cx, int cy, float r, float a0, float a1, int t, uint16_t c);
  void  eyeBall(int cx, int cy, float open, float px, float py,
                uint16_t iris, float scale, float pupilScale);
  void  heart(int cx, int cy, int s, uint16_t c);
  void  cross(int cx, int cy, int s, uint16_t c);
  void  mouthSmile(int mx, int my, int w, uint16_t c, bool up);
  void  drawFace();
  void  drawAction();                  // the care animation overlay
  void  drawSpeech();
  void  drawHud();
};
