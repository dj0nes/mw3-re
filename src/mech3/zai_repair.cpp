#include "mech3/zai_repair.h"

#include "gamez/zutil/zfix.h"
#include "mech3/zai_attack.h"

#include <cmath>

#include "gamez/zmath/zmth_vector.h"
#include "mech3/zai_node.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3::ai {

namespace {
constexpr float kApproach = 40.0f;       // 0x005cc598: the approach point out from the dock point
constexpr float kDownFor = -30.0f;       // 0x005cc5a4: now - this
constexpr float kDockNear2 = 144.0f;     // 0x005e4a18: 12 m, the dock's own (vehicles.md)

// [0x0060b4a0]: cos(pi/4), set at start-up (0x004ce5b0).
float cos_quarter_turn() { return static_cast<float>(std::cos(0.78539816339744828)); }
}  // namespace

bool BrainHooks::refit(Brain& b) { return refit_order(b); }
bool BrainHooks::dock_point(float out[3]) {
  (void)out;
  unported("0x0058b060 on [0x0060b544]", {});
  return false;
}

// Mech3.exe 1.2 @ 0x004d0990: the dock point read when the pilot's vehicle
// is a mech (the state's own copy here: the exe keeps the point's pointer).
BehaviourState* RepairBehaviour::make_state(Order& o) {
  BrainHooks& h = brain_hooks();
  float at[3];
  bool got = false;
  Brain* b = o.owner;
  const zgame::Handle v = b && b->controls ? b->controls->vehicle() : 0;
  if (h.has_mcb() && b && v && h.vehicle_mech(v)) got = h.dock_point(at);  // vt+0x50, then 0x0058b060 / vt+0x18
  return new RepairState(&o, *this, got ? at : nullptr);
}

// Mech3.exe 1.2 @ 0x004d0920
bool repair_possible(Order* o) {
  BrainHooks& h = brain_hooks();
  if (!h.has_mcb() || !o || !o->owner) return false;
  Brain& b = *o->owner;
  const zgame::Handle v = b.controls ? b.controls->vehicle() : 0;
  float at[3];
  const bool got = h.dock_point(at);  // 0x0058b060 / vt+0x18, before the vehicle's test
  return got && v && h.vehicle_mech(v);  // vt+0x50
}

// Mech3.exe 1.2 @ 0x004d0a10. The approach point: the way from the pilot to
// the dock point snapped to the nearest third of a turn, 40 m back from the
// dock point along it (x87 at play's 24 bits, the trig in full).
RepairState::RepairState(Order* o, const RepairBehaviour& b, const float* d) : BehaviourState(o, &copy) {
  copy.flags = uint8_t((copy.flags & ~1u) | (b.flags & 1u));
  copy.x08 = b.x08;
  if (!d) return;
  dock_at[0] = d[0], dock_at[1] = d[1], dock_at[2] = d[2];
  dock = dock_at;
  float pos[3];
  brain_hooks().position(o->owner->controls->vehicle(), pos);  // the context's (0x004cecf0)
  const float dx = pos[0] - d[0], dz = pos[2] - d[2];
  const double a = std::atan2(-static_cast<double>(dx), -static_cast<double>(dz));
  const float t = static_cast<float>(static_cast<float>(a * 0.4774648292756546) - -0.5);  // 0x005cc580, 0x005cc588
  const auto k = static_cast<float>(std::floor(static_cast<double>(t)));
  const auto ang = static_cast<float>(k * 2.0943951023933334);  // 0x005cc590
  const auto s = static_cast<float>(-std::sin(static_cast<double>(ang)));
  const auto c = static_cast<float>(-std::cos(static_cast<double>(ang)));
  approach[0] = d[0] + s * kApproach;
  approach[1] = d[1] + 0.0f;
  approach[2] = d[2] + c * kApproach;
  phase = 1;
}

// Mech3.exe 1.2 @ 0x004d0ba0: ended while walking in or waiting, the own
// fights back; while down, powered up again too.
RepairState::~RepairState() {
  Brain& b = *order->owner;
  float pos[3];
  brain_hooks().position(b.controls->vehicle(), pos);  // the context (0x004cecf0), unused
  if (phase <= 3 || phase > 7) return;
  if (phase == 7) {
    power(b, b.controls->vehicle(), 0);
    b.x2a4 = 0;
  }
  b.params.put<int32_t>(0x80, own_fights ? 1 : 0);
}

// Mech3.exe 1.2 @ 0x004d0c60: the segment from the pilot's place (kept as
// where it last was, +0x298) to `to` at the pilot's speed (vt+0x118 on the
// brain's +0xec share), and the stopping distance at it (vt+0x120).
void RepairState::aim(Brain& b, const float pos[3], const float* to) {
  BrainHooks& h = brain_hooks();
  const zgame::Handle v = b.controls->vehicle();
  b.x298[0] = pos[0], b.x298[1] = pos[1], b.x298[2] = pos[2];
  const float speed = h.speed_at(v, b.xec);
  b.steer.set(b.x298, to, speed, v);  // 0x004cea30
  slow = h.stopping_distance(v, speed);
  inv_slow = 1.0f / slow;  // 0x005cc528
}

// The three walks (0x004d0e31, 0x004d0f2f, 0x004d100f): by the steering
// until the carrot has fallen back to the end and the pilot is within its
// stopping distance (true); else this frame's turn and throttle, `phase_now`.
bool RepairState::walk(Brain& b, zgame::Handle v, const float pos[3], const float fwd[3], int32_t phase_now) {
  const Steer& s = b.steer;
  const float away[2] = {std::fabs(s.along), std::fabs(s.side)};  // 0x004cee50
  const float sum = away[0] + away[1];
  if (!(s.along_at > s.along_min) && !(sum > slow)) return true;
  float turn = 0, throttle = 0;
  walk_drive(b, v, pos, fwd, away, sum, slow * 0.5f, &turn, &throttle, inv_slow);  // 0x004cee80, 0x005cc564
  b.controls->set_turn(turn);          // IControls vt+0x28
  b.controls->set_throttle(throttle);  // vt+0x20
  phase = phase_now;
  return false;
}

// The vehicle's mech (vt+0x50) powered down or up (0x0041cc30).
void RepairState::power(Brain& b, zgame::Handle v, int32_t down) {
  (void)b;
  BrainHooks& h = brain_hooks();
  h.mech_power(h.vehicle_mech(v), down);
}

// Mech3.exe 1.2 @ 0x004d0ce0. Without a dock point (phase 0) the order pops
// at once.
void RepairState::run(float dt) {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  float pos[3], fwd[3] = {0, 0, 0};
  h.position(v, pos);  // the context (0x004cecf0)
  h.forward(v, &fwd[0], &fwd[2]);
  bool timed_out = false;
  switch (phase) {
    case 1:
    case 2: {
      // Pulled by the mission's boundary, straight for the pulled point.
      if (h.inout_clamp(v, approach, pulled)) {  // 0x004c3000
        float to[3] = {pulled[0] - pos[0], 0.0f, pulled[2] - pos[2]};
        zmth::Vec3 w{to[0], 0.0f, to[2]};
        zmth::flat_normalize(w, w);  // 0x0051e210
        to[0] = w.x, to[1] = w.y, to[2] = w.z;
        float dot, cross;
        const float turn = turn_toward(fwd, to, &dot, &cross, c.turn_goal(), turn_params());  // 0x004cdd40
        c.set_turn(turn);
        c.set_throttle(dot > cos_quarter_turn() ? b.xec : 0.0f);  // [0x0060b4a0]
        phase = 2;
        return;
      }
      // Ours (W58): by the grid to the approach point when the way is blocked.
      float goal[3];
      if (zfix::options().bugs && nav_for(v) && nav.step(v, pos, approach, false, 0.0f, dt, goal)) {
        nav_drive(b, v, pos, goal, &nav_turn);
        phase = 2;
        return;
      }
      aim(b, pos, approach);
      [[fallthrough]];
    }
    case 3:
      if (!walk(b, v, pos, fwd, 3)) return;
      own_fights = b.params.get<int32_t>(0x80) == 1;
      b.params.put<int32_t>(0x80, 0);
      aim(b, pos, dock);
      [[fallthrough]];
    case 4:
      if (!walk(b, v, pos, fwd, 4)) return;
      slow = slow * 0.25f;          // 0x005cc59c
      inv_slow = inv_slow * 4.0f;   // 0x005cc5a0
      [[fallthrough]];
    case 5:
      if (!walk(b, v, pos, fwd, 5)) return;
      c.set_turn(0);
      c.set_throttle(0);
      [[fallthrough]];
    case 6:
      if (h.dock_drive() && !h.dock_contact()) {  // the bases still on their way
        phase = 6;
        return;
      }
      c.set_turn(0);
      c.set_throttle(0);
      power(b, v, 1);
      b.x2a4 = 1;
      until = h.now() - kDownFor;
      break;
    case 7: {
      const float dx = pos[0] - dock[0], dz = pos[2] - dock[2];
      timed_out = h.now() > until || dz * dz + dx * dx > kDockNear2;  // 0x0051e060: pushed off the dock
      break;
    }
    default:
      order->pop();  // order vt+0x08
      return;
  }
  if (h.dock_drive() && h.dock_contact() && h.powered_down(v) && !timed_out) {
    c.set_turn(0);
    c.set_throttle(0);
    phase = 7;
    return;
  }
  power(b, v, 0);
  b.x2a4 = 0;
  b.params.put<int32_t>(0x80, own_fights ? 1 : 0);
  order->pop();
}

// Mech3.exe 1.2 @ 0x004ea260
bool refit_order(Brain& b) {
  BrainHooks& h = brain_hooks();
  const int32_t up = h.bases_alive();
  RepairBehaviour temp;  // 0x004d08d0, on the stack
  if (up <= 0) return false;
  if (!h.mfb_ask(b.controls ? b.controls->vehicle() : 0)) return false;  // ECX [0x0060b544], EDX the vehicle
  if (!repair_possible(&b.current())) return false;
  b.push_behaviour(&temp);  // 0x004e88c0
  return true;
}

}  // namespace mech3::ai
