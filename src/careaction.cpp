// =====================================================================
//  careaction.cpp  -  feed / wash / medicine : animation, then stat, then face
//  (see careaction.h for why the order is animation first)
// =====================================================================
#include "careaction.h"
#include "buzzer.h"

// ---------------------------------------------------------------- helpers
static const char* careName(CareKind k) {
  switch (k) {
    case CARE_FEED:     return "feed";
    case CARE_WASH:     return "wash";
    case CARE_MEDICINE: return "medicine";
    default:            return "none";
  }
}

static const char* outcomeName(CareOutcome o) {
  switch (o) {
    case CARE_OUT_OK:     return "ok";
    case CARE_OUT_FULL:   return "full";
    case CARE_OUT_ASLEEP: return "asleep";
    case CARE_OUT_DEAD:   return "dead";
    default:              return "?";
  }
}

// Which stat the action moves and its current value - used by the two log lines
// only, so that "the value really did change only after the animation" can be
// read straight off the serial log.
static void statNow(Pet& pet, CareKind k, const char** name, uint8_t* val) {
  PetState& s = pet.s();
  switch (k) {
    case CARE_WASH:     *name = "clean";  *val = s.clean;  break;
    case CARE_MEDICINE: *name = "health"; *val = s.health; break;
    default:            *name = "hunger"; *val = s.hunger; break;
  }
}

// ---------------------------------------------------------------- beats
bool CareAction::begin(CareKind k) {
  if (k == CARE_NONE) return false;
  if (_kind != CARE_NONE) {
    Serial.printf("[care] %s ignored: %s is still running\n", careName(k),
                  careName(_kind));
    return false;
  }

  _kind    = k;
  _out     = CARE_OUT_OK;
  _settled = false;
  _start   = millis();

  const char* sn;
  statNow(_pet, k, &sn, &_before);

  // Beat 1: animation only. The pet looks slightly down - the berry / pill fly in
  // at mouth height and the bubbles climb the cheeks - and keeps the expression
  // it had. The face that judges the outcome comes later, in perform().
  _eyes.look(0, 60);
  switch (k) {
    case CARE_FEED:     _eyes.playAction(ACT_EAT,      CARE_ANIM_MS); break;
    case CARE_WASH:     _eyes.playAction(ACT_WASH,     CARE_ANIM_MS); break;
    case CARE_MEDICINE: _eyes.playAction(ACT_MEDICINE, CARE_ANIM_MS); break;
    default: break;
  }

  Serial.printf("[care] %s: animating %u ms (%s %u)\n", careName(k),
                (unsigned)CARE_ANIM_MS, sn, (unsigned)_before);
  return true;
}

void CareAction::update() {
  if (_kind == CARE_NONE) return;
  if ((uint32_t)(millis() - _start) < (uint32_t)CARE_ANIM_MS) return;   // still beat 1

  // Beat 2: the animation is over, so *now* the world changes. Deciding here
  // instead of when [A] was pressed is the whole point: the pet may have fallen
  // asleep or died during those CARE_ANIM_MS.
  switch (_kind) {
    case CARE_FEED:     _out = _pet.feed();     break;
    case CARE_WASH:     _out = _pet.wash();     break;
    case CARE_MEDICINE: _out = _pet.medicine(); break;
    default: break;
  }
  _pet.save();          // persist exactly the moment the value starts to exist
  _eyes.endAction();    // ... and stop the animation in the same frame, so the
                        //     two can never drift apart

  // The stat just moved, so the *resting* face may have moved with it (a hungry
  // pet that is fed stops looking curious). Set it here, before beat 3: the UI's
  // per-frame "has the expression changed?" bookkeeping must not wipe the flash
  // that perform() is about to start.
  _eyes.setBaseMood(_pet.expressionMood());
  perform();            // Beat 3: the face that explains the outcome

  const char* sn;
  uint8_t     val;
  statNow(_pet, _kind, &sn, &val);
  Serial.printf("[care] %s -> %s (%s %u -> %u after %u ms)\n", careName(_kind),
                outcomeName(_out), sn, (unsigned)_before, (unsigned)val,
                (unsigned)CARE_ANIM_MS);

  _settled = true;      // the UI picks this up on its next poll
  _kind    = CARE_NONE; // a new action may start from the next frame on
}

bool CareAction::consumeSettled() {
  const bool s = _settled;
  _settled = false;
  return s;
}

// Beat 3: the verdict. The outcome - not the kind - decides the performance, so
// a refusal is answered with a refusal instead of a cheerful "好吃".
void CareAction::perform() {
  switch (_out) {
    case CARE_OUT_OK:
      if (_kind == CARE_FEED)         { _eyes.flashMood(MOOD_HAPPY,   800); _eyes.say("好吃", 1500);   sfxEat(); }
      else if (_kind == CARE_WASH)    { _eyes.flashMood(MOOD_HAPPY,   800); _eyes.say("好舒服", 1600); sfxHappy(); }
      else                            { _eyes.flashMood(MOOD_CURIOUS, 700); _eyes.say("好多了", 1600); sfxConfirm(); }
      break;
    case CARE_OUT_FULL:               // already full: it refuses, and the refusal
      _eyes.flashMood(MOOD_ANGRY, 900);   _eyes.say("吃不下了", 1800); sfxError(); break;
    case CARE_OUT_ASLEEP:
      _eyes.flashMood(MOOD_SLEEPY, 900);  _eyes.say("睡着了", 1500);   sfxSleep(); break;
    case CARE_OUT_DEAD:               // the dead do not talk
      _eyes.flashMood(MOOD_DEAD, 1200);   sfxSad(); break;
    default: break;
  }
  _eyes.lookRandom();
}
