#pragma once
#include <cstdint>

#include "mech3/zai_attack.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_steps.h"

// The AI's side of the field bases (docs/vehicles.md, The AI at the dock;
// docs/ai.md 17.6): the refit order 0x004ea260 asks the dock for the pilot
// (0x0047a780) and pushes the repair behaviour, whose state walks the pilot
// to the dock point and keeps it powered down while the dock works on it.
// Original file unknown: with the follow and the fight (0x004d08d0..
// 0x004d0ce0, between zainetmechinout.cpp and zaitarget.cpp).
namespace mech3::ai {

// 0x004d08d0 (0x0c bytes, vtable 0x005cc970): made on the refit's stack and
// pushed; its state (vt+0x8c 0x004d0990) keeps its own copy.
struct RepairBehaviour : Behaviour {
  RepairBehaviour() : Behaviour(StepKind::Repair) {}
  BehaviourState* make_state(Order& o) override;  // vt+0x8c 0x004d0990
};

// 0x004d0920: the dock's point (the mcbwaynet's selected one, with a place)
// and the pilot's vehicle a mech.
bool repair_possible(Order* o);

// 0x004d0a10 (0x44 bytes, vtable 0x005cca28, the behaviour's copy at +0x08):
// the pilot walks to a point 40 m out from the dock point on the side it
// comes from (snapped to a third of a turn), then onto the dock point by
// the brain's steering, closer at the end; waits while the bases drive in;
// then powers down and stays down (its brain thinking, +0x2a4) while the
// dock is in contact, 30 s at most and within 12 m. Its own fights (params
// +0x80) are off meanwhile and put back after. It lets no order break in
// and the pilot not fire.
class RepairState : public BehaviourState {
 public:
  RepairState(Order* o, const RepairBehaviour& b, const float* dock);  // 0x004d0a10
  NavWay nav;          // ours (W58): by the grid to the approach point
  float nav_turn = 0;
  ~RepairState() override;                                            // 0x004d0ba0
  bool may_break_in() const override { return false; }  // 0x004d72a0
  bool may_fire() const override { return false; }      // 0x004d7290
  void run(float dt) override;                          // 0x004d0ce0
  Behaviour* behaviour() const override { return const_cast<RepairBehaviour*>(&copy); }  // 0x0040a860: +0x08

  RepairBehaviour copy;          // +0x08  (its +0x04 flag and +0x08 copied)
  uint8_t own_fights = 0;        // +0x14  params +0x80 was 1
  float slow = 0;                // +0x18  the stopping distance (0x004d0c60)
  float inv_slow = 0;            // +0x1c
  float until = 3.4028235e38f;   // +0x20  the power-down's end: 30 s
  int32_t phase = 0;             // +0x24  0 none (pop), 1-2 out, 3 to the approach, 4-5 to the dock, 6 wait, 7 down
  float pulled[3] = {};          // +0x28  the approach point the boundary pulled
  const float* dock = nullptr;   // +0x34  the dock point's place (here its copy, dock_at)
  float dock_at[3] = {};         // ours: the exe keeps the point's pointer, which a mission's end frees
  float approach[3] = {};        // +0x38

 private:
  void aim(Brain& b, const float pos[3], const float* to);  // 0x004d0c60
  bool walk(Brain& b, zgame::Handle v, const float pos[3], const float fwd[3], int32_t phase_now);
  void power(Brain& b, zgame::Handle v, int32_t down);
};

// Mech3.exe 1.2 @ 0x004ea260 (the brain's vt+0x3c, the refit order): with a
// field base up, the dock asked for the pilot's vehicle (0x0047a780) and the
// repair possible, the repair behaviour pushed on the current order's net.
bool refit_order(Brain& b);

}  // namespace mech3::ai
