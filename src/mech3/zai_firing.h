#pragma once
#include <cstdint>

#include "gamez/zgame/unported.h"

// The pilot's firing (docs/ai.md 17.7): the state machine the brain runs
// each frame (0x004b5b80) over a fire plan, aiming, leading and firing the
// vehicle's current weapon at its target, then resting while the torso looks
// around. Original file unknown: it sits after weapon.cpp (0x004b5860..
// 0x004b7060).
namespace mech3::ai {

class Brain;
struct Contact;
struct Firing;

// A fire plan (the volley pick's, 0x004b6790; item 12b): what to shoot at and
// when. The machine asks its slots; the plan is freed when it is done.
class FirePlan {
 public:
  virtual ~FirePlan() = default;
  // The exe's answers are bytes: in_reach is taken when 1, track when not 0
  // (when 1 before a shot), ready when not 0.
  virtual uint8_t in_reach(float arg) = 0;  // +0x04
  virtual uint8_t track(float arg) = 0;     // +0x10: turned toward it
  virtual uint8_t ready() = 0;              // +0x18
  Contact* const* target = nullptr;       // +0x04  where its contact is kept (*target)
  bool quick = false;                     // +0x0c  aim for the short delay (+0x08)
  bool line_check = false;                // +0x0d  look for friends in the line of fire
  int32_t kind = 0;                       // +0x10  3: an alpha strike
};

// Where a plan can come from before the volley pick (+0x30, +0x50): its
// slot 0 answers one, and whether the source is spent.
class FireSource {
 public:
  virtual ~FireSource() = default;
  virtual FirePlan* take(bool* spent) = 0;  // +0x00
};

// The volley pick's plan (0x004b6790: 0x24 bytes, vtable 0x005c9460): a
// target among the vehicle's contacts and the volley to fire at it. It is a
// source too: its slot 0 hands it back while its target lives, the line is
// clear and the volley still picks something (docs/ai.md 17.7).
class VolleyPlan : public FirePlan, public FireSource {
 public:
  uint8_t in_reach(float arg) override;   // +0x04 0x004b6af0: the bearer's vt+0x150 == 1
  uint8_t track(float arg) override;      // +0x10 0x004b6e30 (item 12b2)
  uint8_t ready() override;               // +0x18 0x004b6e10: the line flag, asked again
  FirePlan* take(bool* spent) override;   // +0x00 0x004b6fa0
  float score = 0;                        // +0x08
  zgame::Handle bearer = 0;               // +0x14  the vehicle's vt+0x40 (a mech itself)
  float aim[3] = {};                      // +0x18  the way to the lead point, in the bearer's frame
};

// 0x004d6de0 (the bearer's vt+0x84, AiTarget's): where to aim weapon `w` to
// meet contact `c`'s entity, from the weapon's node, given both velocities;
// false when no time to meet comes out (out then the entity's position).
bool lead_point(zgame::Handle bearer, const Contact& c, zgame::Handle w, float out[3]);

// 0x004b6790 (from the firing's phases 1 and 6): the best target among the
// vehicle's contacts and a plan on it, or none; the sensor range narrowed
// or widened on the way.
FirePlan* volley_plan_pick(Firing& f);

// 0x004b6390 (0x1c bytes, vtable 0x005c9458), at the firing's +0x34: the
// torso's look-around while the pilot rests between bursts.
struct LookAround {
  void start(void* firing) { owner = firing, phase = 1; }  // +0x00 0x004b63d0
  void sweep();                                            // 0x004b63f0
  void* owner = nullptr;  // +0x04
  float until = 0;        // +0x08
  float dir[3] = {};      // +0x0c  0x0063d870; where it looks (its vt+0x04, 0x004d6dc0, does nothing)
  int32_t phase = 0;      // +0x18
};

// 0x004b7060 (0x24 bytes, at the firing's +0x54): the pilot's aim error
// (docs/ai.md 17.7). The shot goes at the target's position plus an offset
// across the line of sight that drifts back over it; picked again once it
// strays past its limit. Its size scales with the pilot's +0xb8, the
// difficulty's [0x00608e58] and the range, by +0xc0 with a targeting aid.
struct Aim {
  void track(Brain& b, Contact* target, zgame::Handle w);  // 0x004b72a0: a new offset
  void lead(Brain& b, float dt, Contact* target, zgame::Handle w, zgame::Handle wm);  // 0x004b77e0
  float offset[3] = {};           // +0x00  0x0063d870
  float drift[3] = {1.0f, 0, 0};  // +0x0c  0x005eda60: per second
  uint8_t active = 0;             // +0x18  the error is not 0
  float until = 0;                // +0x1c  now + 0.5 when picked (unread)
  float limit2 = 1.0f;            // +0x20  the offset's limit, squared

 private:
  bool frame(Brain& b, Contact* target, zgame::Handle w, float f[9], float muzzle[3], float* spread,
             float* dist);                                // 0x004b70b0
  void pick_drift(const float f[9], float spread, float dist);  // 0x004b71a0
  void repick(Brain& b, Contact* target, zgame::Handle w, float at[3]);  // 0x004b74f0
};

// 0x004b6570 (0x58 bytes at the brain's +0x108). Its first 11 words are the
// defaults at 0x005e8868 (8, then zeros).
struct Firing {
  Firing();
  ~Firing();  // 0x004b65f0
  Firing(const Firing&) = delete;
  Firing& operator=(const Firing&) = delete;

  void run(float dt);      // 0x004b5b80
  void stand_down();       // 0x004b6700 (the brain's 0x004e9590): the plan dropped, rest +0x10
  void free_for_test() { free_plan(); }  // the probes' set-up

  // The defaults are the template [0x005e8868] the constructor copies, filled
  // by the start-up initializer 0x004b6500 (W34: the port had them all 0).
  int32_t phase = 8;       // +0x00  8 start, 1/6 plan, 2 aim, 3 track, 4 fire, 5 rest, 7 burst's end
  float time = 3.4028235e38f;  // +0x04
  float quick_delay = 3;   // +0x08
  float delay = 6;         // +0x0c
  float rest = 1;          // +0x10
  FirePlan* plan = nullptr;  // +0x14
  float reach_arg = 0.9396926f;   // +0x18  cos 20 degrees [0x005c93d0]: the torso near enough to track
  float track_arg = 0.8660254f;   // +0x1c  cos 30 [0x005c93d8]
  float fire_arg = 0.70710677f;   // +0x20  cos 45 [0x005c93e0]: to fire
  float range = 50;        // +0x24  the vehicle's sensor range (+0xa0), set at the start
  zgame::Handle obstacle = 0;  // +0x28  something a target near the line to is passed over for (until one is not)
  Brain* brain = nullptr;  // +0x2c  0x004b6660
  FireSource* first = nullptr;   // +0x30
  LookAround look;         // +0x34
  FireSource* second = nullptr;  // +0x50
  Aim* aim = nullptr;      // +0x54

 private:
  void drop(float wait);   // 0x004b6690
  void free_plan();        // the plan freed unless it is the first source
  bool clear_line(const FirePlan& p);  // the contacts in the line of fire (0x004b5940)
};

}  // namespace mech3::ai
