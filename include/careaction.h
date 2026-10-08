// =====================================================================
//  careaction.h  -  feed / wash / medicine as a three beat action
//
//  A care action is played in this order:
//
//    beat 1  the pet *does* something   CARE_ANIM_MS of animation (eyes.cpp),
//                                       no stat is touched yet
//    beat 2  the stat is applied        Pet::feed() / wash() / medicine()
//    beat 3  the pet shows its verdict  expression + bubble + sound, picked
//                                       from the CareOutcome, not assumed
//
//  Why not "apply, then animate" (which is how the menu used to do it): a care
//  action is not always a success. Feeding a sleeping pet changes nothing,
//  feeding a full one costs a care mistake, and the pet can fall asleep or die
//  while the animation runs. Settling *after* the animation is what lets the UI
//  answer with the truth ("吃不下了", "睡着了") instead of a cheerful "好吃"
//  that never happened.
//
//  Both interfaces drive the very same state machine (app_lvgl.cpp and main.cpp);
//  only the drawing and the refresh of their own widgets differ.
// =====================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "pet.h"
#include "eyes.h"

enum CareKind : uint8_t { CARE_NONE = 0, CARE_FEED, CARE_WASH, CARE_MEDICINE };

class CareAction {
public:
  CareAction(Pet& pet, EyeEngine& eyes) : _pet(pet), _eyes(eyes) {}

  bool       begin(CareKind k);        // false: another action is still running
  bool       busy() const { return _kind != CARE_NONE; }
  CareKind   kind() const { return _kind; }
  void       update();                 // call once per frame (millis based)
  bool       consumeSettled();         // true once, on the frame the stat changed
  CareOutcome outcome() const { return _out; }

private:
  Pet&        _pet;
  EyeEngine&  _eyes;
  CareKind    _kind    = CARE_NONE;
  CareOutcome _out     = CARE_OUT_OK;
  uint32_t    _start   = 0;            // millis() when beat 1 started
  bool        _settled = false;        // beats 2 + 3 already ran
  uint8_t     _before  = 0;            // the stat before beat 2 (log line only)

  void        perform();               // beat 3: expression + bubble + sound
};
