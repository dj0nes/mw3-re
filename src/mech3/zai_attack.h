#pragma once
#include <array>
#include <cstdint>
#include <deque>

#include "mech3/zai_nav.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_node.h"
#include "mech3/zai_steps.h"

// The attack's orders (docs/ai.md 17.2): what the brain's attack
// (0x004e9ca0) pushes under the fight, the way home afterwards. Original
// file unknown: the net sits with the nets (0x004c0810..0x004c0b50), the
// behaviours and states with the steps (0x004cac00..0x004cb11b).
namespace mech3::ai {

// 0x004ce250 (through 0x004ce230): the flat way from `from` to `to`, its
// length and unit direction (y 0).
struct FlatWay {
  float dist = 0;      // +0x00
  float dir[3] = {};   // +0x04
};
void flat_way(const float from[3], const float to[3], FlatWay* w);

// 0x004c8d10: the turn for a target `cross` to the side and `dot` ahead:
// behind, full toward its side; else none within the dead zone, past it
// (side - dead) * gain, at least `least`, at most 1.
float turn_past(float cross, float sin_dead, float gain, float least, float dot);
// 0x004c8df0: the same from `fwd` and the way `dir` (*cross written), with
// the dead zone ahead (none) and straight back (`keep`, or 1 for none).
float turn_hold(const float fwd[3], const float dir[3], const float* dot, float* cross, float keep,
                const TurnParams& p);

// 0x004c0810 (0x1c bytes, vtable 0x005cabd0, over 0x004bfbb0(0, 1)): the
// attack's own net, one step: the stand behaviour (0x00604058, not its to
// free). It allows no attack; its part (+0x5c 0x004c0a70) is an AttackPart.
class AttackNet : public CompiledNet {
 public:
  AttackNet();
  ~AttackNet() override;  // 0x004c0a40 -> 0x004c0a60
  NetPart* make_part(Order& o) override;
  bool allows_attack() override { return false; }
};

// 0x004c0570 (vtable 0x005cab60, at 0x00603fb8, made at start-up by
// 0x00410450): the formation net the lance order "form on me" (0x004ea4b0)
// and the lancemates' shared state (0x004cbeb0) push: one step, the stand
// behaviour; it allows an attack (+0x54 0x00499270); its id the first free
// one from 10000 (0x0058b1c0, the ids znet's clear keeps). Its part
// (0x004c07d0, 0x0c bytes, vtable 0x005cabc0) only holds it.
class FormationNet : public CompiledNet {
 public:
  FormationNet();
  ~FormationNet() override;  // 0x004c0780
  NetPart* make_part(Order& o) override;  // 0x004c07b0
};
class FormationPart : public NetPart {
 public:
  FormationPart(Order* o, FormationNet* n) : NetPart(o), target(n) {}
  AiNet* net() const override { return target; }
  FormationNet* target;  // +0x08
};

// 0x004c0a90 (0x1c bytes, vtable 0x005cac30): a home point for the leash,
// the pilot's position, stacked on the brain's +0xe4; its dtor (0x004c0b50)
// takes it off, and with none under it ends the attack (+0xe8 FLT_MAX,
// +0x230 0), else counts [0x0060b328].
class AttackPart : public NetPart {
 public:
  AttackPart(Order* o, AttackNet* n);
  ~AttackPart() override;
  AiNet* net() const override { return target; }  // 0x0040a890

  float home[3] = {};            // +0x08
  const float* prev = nullptr;   // +0x14
  AttackNet* target = nullptr;   // +0x18
};
int32_t& attack_part_unstacked();  // [0x0060b328], written only

// The three behaviours the attack makes on its stack (0x004c8600 with their
// vtables; their states copy what they need).
// 0x005cd920 (state 0x004cac00 -> 0x004cac20): the attack's net to free.
struct AttackEnd : Behaviour {
  explicit AttackEnd(AttackNet* n) : Behaviour(StepKind::AttackEnd), net(n) {}
  AttackNet* net;  // +0x0c
};
// 0x004caca0, 0x005cb948 (state 0x004cacc0 -> 0x004cace0).
struct AttackRearm : Behaviour {
  AttackRearm() : Behaviour(StepKind::AttackRearm) {}
};
// 0x004cad70, 0x005cba20 (state 0x004cad90 -> 0x004cadb0): the net the
// pilot was on.
struct AttackReturn : Behaviour {
  explicit AttackReturn(AiNet* n) : Behaviour(StepKind::AttackReturn), net(n) {}
  AiNet* net;  // +0x0c
};

// 0x004cac20 (0x0c bytes, vtable 0x005cb928): resumed, its order popped and
// the attack's net freed (0x004cac80).
class AttackEndState : public BehaviourState {
 public:
  AttackEndState(Order* o, AttackEnd* b) : BehaviourState(o, b), net(b->net) {}
  void resume() override;
  AttackNet* net;  // +0x08
};
// 0x004cace0 (8 bytes, vtable 0x005cba00): resumed, its order popped and
// the re-engage timer set (0x004cad30: +0x60 off for good, +0x5c a delay of
// +0x58; +0x3c off).
class AttackRearmState : public BehaviourState {
 public:
  using BehaviourState::BehaviourState;
  void resume() override;
};
// 0x004cadb0 (0x1c bytes, vtable 0x005cbad8): the way home. Resumed, the
// old net's step nearest the pilot (0x004d51d0) is its waypoint (0x004cae10);
// run (0x004cae90), the pilot drives there and the order ends within +0x28.
class AttackReturnState : public BehaviourState {
 public:
  AttackReturnState(Order* o, AttackReturn* b) : BehaviourState(o, b), net(b->net) {}
  void resume() override;
  void run(float dt) override;
  AiNet* net;             // +0x08
  float way[3] = {};      // +0x0c
  float turn = 0;         // +0x18
};

// 0x004c8a70 (EDX the brain): drive the vehicle from `from` toward `to`
// (*turn kept between calls, *throttle out) and set the controls' throttle
// and turn: within +0x28 it has arrived (true); closer than +0x2c, with
// `back`, it backs off at the speed share facing within 0.9, else holds;
// pulled by the boundary it turns toward the pull and is done. The way home
// (0x004cae90) is the same without backing off.
bool drive_to(Brain& b, zgame::Handle v, const float from[3], const float to[3], float* turn, float* throttle,
              bool back);

// Ours (W17, FIX (bug) behind bugs; ai.md section 17.6): round a wall. No
// progress toward a target past `reach` for 2 s (a wall, or a contact holding
// the carrot back), the clear heading nearest the target (the wall kept on
// one side) is walked until the line to it is clear; after 20 s it gives up
// for 10, then may try again. The follow's, and (W27) the fight's approach.
struct WallDetour {
  bool on = false;
  int32_t side = 0;          // +1 or -1: the way he turns off the line
  float dir[2] = {};         // the heading walked (x, z)
  float watch_t = 0, watch_d = 0, time = 0, probe_t = 0, cool = 0;
  float watch_fwd[2] = {};   // the heading at the window's start: a turn is not being stuck
  int32_t walked = 0, gave_up = 0;  // detours begun, and given up (for the tests)
  // Where to walk this frame; false to go as the exe.
  bool goal(const float pos[3], const float fwd[2], const float to[3], float reach, float dt, float out[3]);
};

// Ours (W58, behind bugs and the side's switch; docs/lancemate-pathing.md):
// a pilot's grid path toward its point, and when it fights, a firing spot
// with a line of sight to the enemy within its range.
struct NavWay {
  nav::Follower follow;
  float spot[3] = {};
  float spot_t = -1e9f;      // when picked
  float spot_for[3] = {};    // the enemy's place then
  bool has_spot = false;
  int32_t spots = 0;         // spots picked (for the tests)
  // The point to walk to this frame (a waypoint), or false as before. With
  // `enemy`, toward a firing spot on it within `range` when the line of
  // sight from here is blocked.
  bool step(zgame::Handle v, const float pos[3], const float to[3], bool enemy, float range, float dt, float out[3], bool must_find = false);
};
// Whether this pilot walks by the grid (zfix bugs, its side's switch).
bool nav_for(zgame::Handle v);
void nav_drive(Brain& b, zgame::Handle v, const float pos[3], const float goal[3], float* turn);

// The fight (0x004d1ca0 -> 0x004d1cc0, vtable 0x005ccb60, over the waiting
// state 0x004c8a30): what the target vehicle net's attack behaviour (slot 1)
// makes in the order the brain pushes on that net. Run (0x004d1d10): the
// pilot closes on the target vehicle (the net's +0x1c) to within +0x28,
// backing off inside +0x2c, then the order takes the behaviour for its
// mode (the net part's +0x1c); it gives up when either has no position,
// the target is not active (+0x08) or the brain must break off (0x004e8dd0).
// It lets no order break in and the pilot not fire (0x004d72a0, 0x004d7290).
class FightState : public BehaviourState {
 public:
  FightState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) {}
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void run(float dt) override;
  uint32_t x08 = 0;          // +0x08  the waiting state's
  float turn = 0;            // +0x0c
  uint8_t x10 = 0;           // +0x10
  Behaviour* made_by;        // +0x18
  WallDetour detour;         // ours (W27): round a wall to the enemy
  NavWay nav;                // ours (W58): by the grid, to a firing spot
};

// The follow (0x004c89f0 -> 0x004d1870, vtable 0x005cca88, over the waiting
// state 0x004c8a30): what the target vehicle net's follow behaviour (slot 0)
// makes, for the lance order "attack my target" (0x004ea450) and the
// formation (docs/ai.md 17.8b). The steering segment runs from the target
// back to the pilot, the carrot falling toward the target at the pilot's
// speed (+0x84, or +0xec when 0); each run (0x004d1980) sets it again and
// walks by it, the carrot kept no nearer than halfway between +0x2c and
// +0x28; pulled by the mission boundary, toward the pulled point (+0x10
// set). It ends when the target is gone or not active (+0x08). It lets no
// order break in and the pilot not fire (0x004d72a0, 0x004d7290).
class FollowState : public BehaviourState {
 public:
  FollowState(Order* o, Behaviour* b);  // 0x004d1870
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void run(float dt) override;          // 0x004d1980
  uint32_t x08 = 0;          // +0x08  the waiting state's
  float x0c = 0;             // +0x0c
  uint8_t pulled = 0;        // +0x10  the last run's segment was to the boundary's point
  Behaviour* made_by;        // +0x18

  WallDetour detour;           // ours (W17): round a wall
  NavWay nav;                  // ours (W58): by the grid

  // Ours (W27, FIX (bug) behind bugs; ai.md section 17.6): the leader's way
  // as breadcrumbs, a point each 8 m he walks (the last 60). Past his
  // follow range with the leader out of sight (a wall between their eyes),
  // he walks to the newest point he can see, then on to newer ones; the
  // leader in sight again, the exe's straight follow.
  struct Trail {
    std::deque<std::array<float, 3>> crumbs;
    int32_t at = -1;           // the crumb walked to; -1 none
    float look_t = 0;          // to the next sight check
    bool sees = true;          // the leader in sight at the last check
    int32_t used = 0;          // crumbs walked to (for the tests)
    float cap_t = 0;           // after a crumb he could not reach: step by step, no skipping
    float watch_t = 0, watch_d = 0;  // progress toward the crumb (or the leader)
    float force_t = 0;         // the leader "in sight" but no way on: the trail anyway
  } trail;
  float crumb_turn = 0;        // the turn toward the crumb, kept frame to frame

 private:
  void aim(Brain& b, zgame::Handle v, const float pos[3], const float to[3], float floor);
  void trail_record(const float to[3]);
  bool trail_goal(const float pos[3], const float to[3], float reach, float dt, float goal[3]);
};

// For the tests: every detour heading taken as blocked (the give-up).
bool& follow_detour_blocked();

// A vehicle net's mode 1 (slot 2) state: circling the target (0x004c9730 ->
// 0x004c9750, 0x3c bytes, vtable 0x005cb3b0, over the waiting state
// 0x004c8a30). Made with the flat way from the pilot to the target as the
// radial (+0x1c) and its perpendicular (z, 0, -x) as the tangent (+0x28);
// the circling way (+0x34, 1 the other way round) and the speed share
// (+0x38, the brain's +0x94).
class CircleState : public BehaviourState {
 public:
  CircleState(Order* o, Behaviour* b);  // 0x004c9750
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void run(float dt) override;          // 0x004c9880
  Behaviour* behaviour() const override { return made_by; }  // 0x0040a890
  uint32_t x08 = 0;          // +0x08  the waiting state's
  float turn = 0;            // +0x0c
  uint32_t x10 = 0;          // +0x10
  Behaviour* made_by;        // +0x18
  float radial[3] = {};      // +0x1c
  float tangent[3] = {};     // +0x28
  int32_t flip = 0;          // +0x34
  float share = 1.0f;        // +0x38
};

// A vehicle net's mode 2 (slot 3) state: orbiting the target at the
// brain's +0x44 (0x004d1300 -> 0x004d1320, 0x24 bytes, vtable 0x005cca48,
// over the waiting state): +0x1c the speed share (the brain's +0x94, at
// most its +0xec), +0x20 the way round (1 or -1, turned on each resume).
class OrbitState : public BehaviourState {
 public:
  OrbitState(Order* o, Behaviour* b);   // 0x004d1320
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void run(float dt) override;          // 0x004d13c0
  void resume() override { way = way * -1.0f; }  // 0x004d13b0 (0x005cc5a8)
  Behaviour* behaviour() const override { return made_by; }  // 0x0040a890
  uint32_t x08 = 0;          // +0x08  set to 1 by each run
  float turn = 0;            // +0x0c
  uint32_t x10 = 0;          // +0x10
  Behaviour* made_by;        // +0x18
  float share = 1.0f;        // +0x1c
  float way = 1.0f;          // +0x20
};

// A vehicle net's mode 4 (slot 6) state (0x004d1630 -> 0x004d1650, 0x24
// bytes, vtable 0x005cca68): mode 2's layout and steering with neither the
// boundary nor +0x08.
class VeerState : public BehaviourState {
 public:
  VeerState(Order* o, Behaviour* b);    // 0x004d1650
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void run(float dt) override;          // 0x004d16f0
  void resume() override { way = way * -1.0f; }  // 0x004d16e0 (0x005cc5a8)
  Behaviour* behaviour() const override { return made_by; }  // 0x0040a890
  uint32_t x08 = 0;          // +0x08
  float turn = 0;            // +0x0c
  uint32_t x10 = 0;          // +0x10
  Behaviour* made_by;        // +0x18
  float share = 1.0f;        // +0x1c  the brain's +0x94, at most its +0xec
  float way = 1.0f;          // +0x20
};

// The brain's own order's state (docs/ai.md 17.2): keeping station on the
// vehicle whose own net it is on. 0x004cc290 (0x30 bytes, vtable 0x005cbf10,
// over the waiting state 0x004c8a30): +0x20 the throttle's way (-1 when
// the pilot is reversing, IControls vt+0x18), +0x24 the pilot's +0x78 x
// 0.9, +0x28 sqrt(+0x74) x 1.1 (made again when resumed, 0x004cc380), +0x2c
// a count and +0x2e a phase its run (0x004cc3e0, item 10d3) keeps. Lets no
// order break in, the pilot not fire; its behaviour() is its +0x18.
class StationState : public BehaviourState {
 public:
  StationState(Order* o, Behaviour* b);
  bool may_break_in() const override { return false; }
  bool may_fire() const override { return false; }
  void resume() override;  // 0x004cc380
  void run(float dt) override;
  Behaviour* behaviour() const override { return made_by; }  // 0x0040a890
  uint32_t x08 = 0;        // +0x08  the waiting state's
  float turn = 0;          // +0x0c
  uint8_t x10 = 0;         // +0x10
  Behaviour* made_by;      // +0x18
  float way = 1.0f;        // +0x20
  float close = 0;         // +0x24  too close within this (+0x78 x 0.9)
  float range = 0;         // +0x28  kept within this, else the order ends
  int16_t count = 0;       // +0x2c
  int16_t phase = 0;       // +0x2e

 protected:
  void take_params();      // the ctor's and the resume's
};
// 0x004cc240 (vtable 0x005cbef0): the same for a pilot whose vehicle's
// vt+0xf4 says so (none of the mechs): its run (0x004cc020) only holds while
// the other vehicle is within +0x28 (phase 1; the stop under [0x0060b48c],
// which nothing sets), else the order ends.
class StationHoldState : public StationState {
 public:
  using StationState::StationState;
  void run(float dt) override;
};
// Mech3.exe 1.2 @ 0x004cc150 (the own-order behaviour's vt+0x8c): none
// without a vehicle, a vehicle net part or its vehicle; for a vt+0xf4
// pilot the hold unless the other is one too; else the station keeping,
// but none for a lancemate (+0xbc bit 4) by another or by the player.
BehaviourState* make_station_state(Behaviour& b, Order& o);

// Mech3.exe 1.2 @ 0x004e9ca0 (from the brain's frame, before it pushes the
// fight): the current order remade on `n`, then the attack's net and its
// three orders. False when the net cannot be made.
bool attack(Brain& b, AiNet* n);

// The combat manoeuvre's state (docs/ai.md 17.2): 0x004ca440 under
// 0x004caae0 (0x30 bytes, vtable 0x005cb8e8; the behaviour at 0x00604088,
// made by 0x004caac0) and 0x004cabe0 (vtable 0x005cb908; at 0x00604098 by
// 0x004cabc0, given no jets). It brakes to a stop, then turns to face a
// target (the nearest enemy, else the last attacker within 1 s, else
// anyone) and hops on the jets until its time is up. Both answer the
// behaviour at 0x00604088 (0x004cab80).
class ManoeuvreState : public BehaviourState {
 public:
  ManoeuvreState(Order* o, Behaviour* b, bool no_jets);  // 0x004caae0 over 0x004ca440
  ~ManoeuvreState() override;                            // 0x004ca570
  bool may_break_in() const override { return false; }   // 0x004d72a0
  bool may_fire() const override { return false; }       // 0x004d7290
  void suspend() override;                               // 0x004ca5f0
  void run(float dt) override;                           // 0x004ca600
  Behaviour* behaviour() const override;                 // 0x004cab80
  int32_t no_jets = 0;        // +0x08  given, or no vehicle, mech or jump jets
  float jump = 0;             // +0x0c  the jets: 1 with them, off below 10% fuel, on again past 60%
  int32_t phase = 0;          // +0x10  0 braking, 1 picking the target, 2 facing it
  float turn = 0;             // +0x14
  float until = 3.4028235e38f;  // +0x18  now + the brain's +0x6c..+0x70 at random
  uint32_t x1c = 0;           // +0x1c
  float x20[3] = {};          // +0x20  0x0063d870
  zgame::Handle target = 0;   // +0x2c
};

// What a suspended manoeuvre leaves in its order (0x004ca5f0): the
// behaviour at 0x0060b450 (0x004ca370, vtable 0x005cb7e8, made at start-up
// by 0x004ca330) and its state (0x004ca390 -> 0x004ca3b0, 8 bytes, vtable
// 0x005cb8a0), which ends the order when it runs or comes back on top.
class AfterManoeuvreState : public BehaviourState {
 public:
  AfterManoeuvreState(Order* o, Behaviour* b) : BehaviourState(o, b) {}
  void resume() override { order->pop(); }  // 0x004ca410
  void run(float dt) override;              // 0x004ca420
  Behaviour* behaviour() const override;    // 0x004ca3d0
};
Behaviour* after_manoeuvre_behaviour();  // 0x0060b450

}  // namespace mech3::ai
