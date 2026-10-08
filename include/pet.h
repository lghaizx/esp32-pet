// =====================================================================
//  pet.h  -  electronic pet state machine
// =====================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "eyes.h"      // for EyeMood

enum PetStage : uint8_t {
  STAGE_EGG = 0, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT, STAGE_COUNT
};
const char* stageName(uint8_t stage);

// Display helpers shared by both UIs: petAgeText() keeps the status page in a
// readable unit (m -> h -> d) and petGrowthPlanText() describes the current pace
// for the boot log (see pet.cpp).
void petAgeText(char* out, unsigned n, uint16_t ageMin);
void petGrowthPlanText(char* out, unsigned n);

struct PetState {
  uint8_t  hunger;      // 0..100 (100 = full)
  uint8_t  happy;       // 0..100
  uint8_t  energy;      // 0..100
  uint8_t  health;      // 0..100
  uint8_t  clean;       // 0..100
  uint8_t  weight;      // grams
  uint16_t ageMin;      // age in "minutes" (1 min of real time at 100% growth speed)
  uint8_t  stage;       // PetStage
  uint8_t  poop;        // 0..4 piles on screen
  bool     asleep;
  bool     sick;
  bool     dead;
  uint16_t poopsTotal;
  uint16_t mistakes;    // care mistakes (neglect/dirty/overfeed)
};

// Result of a care action. The UI does not assume it worked: it picks the
// *performance* that explains what happened from this value (see careaction.h),
// so feeding a sleeping - or already full - pet can answer with the right face
// instead of always claiming "好吃".
enum CareOutcome : uint8_t {
  CARE_OUT_OK = 0,   // the action was applied
  CARE_OUT_FULL,     // fed while already full -> over-feeding is a care mistake
  CARE_OUT_ASLEEP,   // asleep: nothing was applied
  CARE_OUT_DEAD      // dead: nothing was applied
};

class Pet {
public:
  void      begin();
  void      reset();

  void      update(uint32_t dt);          // safe to call every loop

  // interactions
  CareOutcome feed();
  void      play();
  void      toggleSleep();
  CareOutcome wash();
  CareOutcome medicine();
  void      petIt();                      // "petting" reaction
  void      scold();
  void      addHappy(int d);
  void      addHunger(int d);
  void      wake();

  // status
  PetState& s() { return _st; }
  int       moodScore() const;            // 0..100 overall well-being
  EyeMood   expressionMood() const;       // expression driven by state
  bool      consumeEvolve();              // true once when an evolution happened
  bool      consumePoop();                // true once when a new poop appeared

  // persistence (SD)
  bool      save();
  bool      load();

  bool      alive() const { return !_st.dead; }

private:
  PetState _st;
  uint32_t _acc = 0;            // ms accumulator for the 1s tick
  int32_t  _poopCountdown = 90;
  int32_t  _sickCountdown = 200;
  bool     _evolved = false;
  bool     _pooped  = false;
  uint32_t _ticks   = 0;
  uint32_t _ageAcc  = 0;        // fractional age in "ms * growth percent" (see pet.cpp)

  void tick();
  void addPoop();
  void recomputeStage(bool announce);
};
