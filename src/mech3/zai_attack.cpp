#include "mech3/zai_attack.h"

#include <algorithm>
#include <cmath>

#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zfix.h"
#include "mech3/zai_target.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3::ai {

// Mech3.exe 1.2 @ 0x004ce250
void flat_way(const float from[3], const float to[3], FlatWay* w) {
  w->dir[0] = to[0] - from[0], w->dir[1] = to[1] - from[1], w->dir[2] = to[2] - from[2];
  w->dir[1] = 0;
  zmth::Vec3 d{w->dir[0], w->dir[1], w->dir[2]};
  w->dist = zmth::normalize(d);  // 0x004020d0
  w->dir[0] = d.x, w->dir[1] = d.y, w->dir[2] = d.z;
}

// Mech3.exe 1.2 @ 0x004c8d10. A NaN side is a side to the left (-1 behind,
// the left branch ahead); a NaN past the dead zone takes `least`.
float turn_past(float cross, float sin_dead, float gain, float least, float dot) {
  if (!(dot >= 0.0f)) return cross >= 0.0f ? 1.0f : -1.0f;
  if (!(cross >= -sin_dead)) {
    const float v = (cross + sin_dead) * gain;
    if (!(v >= -1.0f)) return -1.0f;
    const float nl = -least;
    return v > nl ? nl : v;
  }
  if (!(cross > sin_dead)) return 0.0f;
  const float v = (cross - sin_dead) * gain;
  if (v > 1.0f) return 1.0f;
  return v >= least ? v : least;
}

// Mech3.exe 1.2 @ 0x004c8df0 (ECX fwd, EDX dir)
float turn_hold(const float fwd[3], const float dir[3], const float* dot, float* cross, float keep,
                const TurnParams& p) {
  *cross = 0;
  if (*dot < -p.cos_dead) return keep == 0.0f || std::isnan(keep) ? 1.0f : keep;  // x87 C3: equal or unordered
  if (*dot >= p.cos_dead) return 0.0f;
  const float c = fwd[2] * dir[0] - dir[2] * fwd[0];
  *cross = c;
  return turn_past(c, p.sin_dead, p.gain, p.least, *dot);
}

// Mech3.exe 1.2 @ 0x004c0810(1): the base reserves one and holds none; the
// stand behaviour is pushed on.
AttackNet::AttackNet() : CompiledNet(znet::alloc_id(300, 300, 400), 1) { elems.push_back(stand_behaviour()); }

// Mech3.exe 1.2 @ 0x004c0a60: the stand behaviour's release (+0xa0) does
// nothing.
AttackNet::~AttackNet() { elems.clear(); }

// Mech3.exe 1.2 @ 0x004c0a70
NetPart* AttackNet::make_part(Order& o) { return new AttackPart(&o, this); }

// Mech3.exe 1.2 @ 0x004c0570
FormationNet::FormationNet() : CompiledNet(znet::alloc_id(znet::kKeptIds, znet::kKeptIds, znet::kKeptIds + 100), 1) {
  elems.push_back(stand_behaviour());
}

// Mech3.exe 1.2 @ 0x004c0780: the stand behaviour is not its to free.
FormationNet::~FormationNet() { elems.clear(); }

// Mech3.exe 1.2 @ 0x004c07b0 -> 0x004c07d0
NetPart* FormationNet::make_part(Order& o) { return new FormationPart(&o, this); }

int32_t& attack_part_unstacked() {
  static int32_t n = 0;
  return n;
}

// Mech3.exe 1.2 @ 0x004c0a90. The vehicle is taken as there (no check).
AttackPart::AttackPart(Order* o, AttackNet* n) : NetPart(o), target(n) {
  Brain& b = *o->owner;
  brain_hooks().position(b.controls->vehicle(), home);  // 0x005a85e0
  b.home_push(home, &prev);
}

// Mech3.exe 1.2 @ 0x004c0b50: the brain's +0xe4 back (FIX (bug): from
// wherever it is in the stack; Brain::homes), the test on the under it had.
AttackPart::~AttackPart() {
  Brain& b = *order->owner;
  const float* under = prev;
  b.home_pop(home);
  if (!under) {
    b.xe8 = 3.4028235e38f;
    b.x230 = 0;
  } else {
    ++attack_part_unstacked();
  }
}

// Mech3.exe 1.2 @ 0x004cac80: the order popped (this state with it), then
// the net freed through its slot 0 (the deleting dtor).
void AttackEndState::resume() {
  AttackNet* n = net;
  order->pop();
  if (n) {
    brain_hooks().on_delete(n);
    delete n;
  }
}

// Mech3.exe 1.2 @ 0x004cad30
void AttackRearmState::resume() {
  Brain& b = *order->owner;
  order->pop();
  if (b.params.get<int32_t>(0x60)) {
    b.xdc = 3.4028235e38f;
    b.params.put<int32_t>(0x3c, 0);
  }
  if (b.params.get<int32_t>(0x5c)) {
    const float t = b.params.get<float>(0x58) + brain_hooks().now();
    b.params.put<int32_t>(0x3c, 0);
    b.xdc = t;
  }
}

// Mech3.exe 1.2 @ 0x004cae10. A step without a position: the pilot stops
// and the order ends.
void AttackReturnState::resume() {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  float pos[3];
  h.position(b.controls->vehicle(), pos);  // 0x005a85e0
  Behaviour* step = h.nearest(net, pos);   // 0x004d51d0
  // FIX (bug): the exe asks the step for its position with no check (no
  // step nearby on a net without steps: a crash); here as one without.
  const float* p = step ? step->position() : nullptr;  // vt+0x18
  if (p) {
    way[0] = p[0], way[1] = p[1], way[2] = p[2];
    return;
  }
  b.controls->stop();  // IControls vt+0x14
  order->pop();
}

// Mech3.exe 1.2 @ 0x004cae90 (docs/ai.md 17.2). The speed share is the
// pilot's +0x84 (+0xec when 0); kept inside the boundary (0x004c3000), the
// pilot turns toward where it is pulled and the order ends.
void AttackReturnState::run(float) {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  // The exe pops the order when 0x005a85e0 answers null (a vehicle without
  // a node); the program's always answer.
  float pos[3];
  h.position(v, pos);
  float cap = b.params.get<float>(0x84);
  bool done = false;
  if (cap == 0.0f || std::isnan(cap)) cap = b.xec;
  float fwd[3] = {};
  h.forward(v, &fwd[0], &fwd[2]);  // the vehicle's +0x24: x and z
  float pulled[3] = {};
  float throttle;
  if (h.inout_clamp(v, way, pulled)) {  // 0x004c3000 on the way point
    FlatWay w;
    flat_way(pos, pulled, &w);
    const float dot = fwd[0] * w.dir[0] + fwd[2] * w.dir[2];
    float cross;
    turn = turn_hold(fwd, w.dir, &dot, &cross, turn, turn_params());
    throttle = dot >= 0.9f ? cap : 0.0f;  // 0x005cb0d8
    done = true;
  } else {
    FlatWay w;
    flat_way(pos, way, &w);
    const float dot = fwd[0] * w.dir[0] + fwd[2] * w.dir[2];
    const TurnParams& p = turn_params();
    float t;
    if (dot < -p.cos_dead) {  // behind
      t = turn == 0.0f || std::isnan(turn) ? 1.0f : turn;
    } else if (!(dot >= p.cos_dead)) {  // past the dead zone (a NaN dot too)
      const float cross = fwd[2] * w.dir[0] - fwd[0] * w.dir[2];
      t = turn_past(cross, p.sin_dead, p.gain, p.least, dot);
    } else {
      t = 0.0f;
    }
    turn = t;
    if (!(w.dist >= b.params.get<float>(0x28))) {  // within +0x28 (or a NaN)
      throttle = 0;
      done = true;
    } else {
      throttle = h.stop_share(v, w.dist - (b.params.get<float>(0x2c) + b.params.get<float>(0x28)) * 0.5f);
      if (c.throttle_now() > throttle) throttle = 0;  // the smoothed throttle above it: let it fall
      if (throttle > cap) throttle = cap;
    }
  }
  c.set_throttle(throttle);  // IControls vt+0x20
  c.set_turn(turn);          // vt+0x28
  if (done) order->pop();
}

// Mech3.exe 1.2 @ 0x004c8a70
bool drive_to(Brain& b, zgame::Handle v, const float from[3], const float to[3], float* turn, float* throttle,
              bool back) {
  BrainHooks& h = brain_hooks();
  BrainControls& c = *b.controls;
  bool done = false;
  float cap = b.params.get<float>(0x84);
  if (cap == 0.0f || std::isnan(cap)) cap = b.xec;
  float fwd[3] = {};
  h.forward(v, &fwd[0], &fwd[2]);  // the vehicle's +0x24: x and z
  float pulled[3] = {};
  if (h.inout_clamp(v, to, pulled)) {  // 0x004c3000
    FlatWay w;
    flat_way(from, pulled, &w);
    const float dot = fwd[0] * w.dir[0] + fwd[2] * w.dir[2];
    float cross;
    *turn = turn_hold(fwd, w.dir, &dot, &cross, *turn, turn_params());
    *throttle = dot >= 0.9f ? cap : 0.0f;  // 0x005cb0d8
    done = true;
  } else {
    FlatWay w;
    flat_way(from, to, &w);
    const float dot = fwd[0] * w.dir[0] + fwd[2] * w.dir[2];
    const TurnParams& p = turn_params();
    float t;
    if (dot < -p.cos_dead) {  // behind
      t = *turn == 0.0f || std::isnan(*turn) ? 1.0f : *turn;
    } else if (!(dot >= p.cos_dead)) {  // past the dead zone (a NaN dot too)
      const float cross = fwd[2] * w.dir[0] - w.dir[2] * fwd[0];
      t = turn_past(cross, p.sin_dead, p.gain, p.least, dot);
    } else {
      t = 0.0f;
    }
    *turn = t;
    if (back && !(w.dist >= b.params.get<float>(0x2c))) {  // too close: back off facing it
      *throttle = dot >= 0.9f ? -cap : 0.0f;
    } else if (!(w.dist >= b.params.get<float>(0x28))) {  // arrived
      *throttle = 0;
      done = true;
    } else {
      *throttle = h.stop_share(v, w.dist - (b.params.get<float>(0x2c) + b.params.get<float>(0x28)) * 0.5f);
      if (c.throttle_now() > *throttle) *throttle = 0;
      if (*throttle > cap) *throttle = cap;
    }
  }
  c.set_throttle(*throttle);  // IControls vt+0x20
  c.set_turn(*turn);          // vt+0x28
  return done;
}

bool nav_for(zgame::Handle v) {
  BrainHooks& h = brain_hooks();
  const AiTarget* t = h.targetable(v);
  return nav::on_for(t && t->is_ally(h.player_vehicle()));
}

namespace {
// The grid's work once a frame (the first pilot to ask): a tile in about 11 frames, some 2 ms a frame.
void nav_frame(float now) {
  static float last = -1e30f;
  if (now == last) return;
  last = now;
  nav::service(now, 768);
}

// A place to fire from at `tgt` within `range`: on rings at 0.6 and 0.8 of
// it, 16 ways round, walkable, with a line of sight from 6 m over its
// ground to 5 m over the enemy's place, not in the player's line of fire
// (within 15 degrees of the way from the player to the enemy, nearer than
// it); the nearest to `pos`.
bool firing_spot(const float pos[3], const float tgt[3], float range, float out[3]) {
  BrainHooks& h = brain_hooks();
  const nav::Grid& g = nav::grid();
  float pl[3] = {};
  const bool has_player = h.player_vehicle() != 0;
  if (has_player) h.position(h.player_vehicle(), pl);
  const float aim[3] = {tgt[0], tgt[1] + 5.0f, tgt[2]};
  const float pd = std::hypot(tgt[0] - pl[0], tgt[2] - pl[2]);
  float best = 1e30f;
  for (float ring : {0.6f, 0.8f})
    for (int k = 0; k < 16; ++k) {
      const float a = 0.39269908f * static_cast<float>(k);
      const float x = tgt[0] + std::cos(a) * ring * range, z = tgt[2] + std::sin(a) * ring * range;
      const int cx = nav::cell_of(x), cz = nav::cell_of(z);
      const int level = g.level_at(cx, cz, tgt[1]);  // on the enemy's storey
      if (level < 0 || !g.walkable(cx, cz, level)) continue;
      const float y = g.cell(cx, cz, level)->y;
      if (has_player && pd > 1.0f) {
        const float ux = (tgt[0] - pl[0]) / pd, uz = (tgt[2] - pl[2]) / pd;
        const float sx = x - pl[0], sz = z - pl[2], sl = std::hypot(sx, sz);
        if (sl < pd && sl > 1.0f && (sx * ux + sz * uz) / sl > 0.9659f) continue;  // cos 15 degrees
      }
      const float eye[3] = {x, y + 6.0f, z};
      if (h.wall_between(eye, aim)) continue;
      const float d = std::hypot(x - pos[0], z - pos[2]);
      if (d < best) best = d, out[0] = x, out[1] = y, out[2] = z;
    }
  return best < 1e30f;
}
}  // namespace

bool NavWay::step(zgame::Handle v, const float pos[3], const float to[3], bool enemy, float range, float dt, float out[3], bool must_find) {
  BrainHooks& h = brain_hooks();
  const float now = h.now();
  nav_frame(now);
  (void)v;
  float goal[3] = {to[0], to[1], to[2]};
  // Out of range: the enemy itself (the brain's range band stops him). In
  // range without a line of sight: a firing spot with one.
  // Near over or under it on another storey is not near.
  const bool near_it = range > 1.0f && std::hypot(to[0] - pos[0], to[2] - pos[2]) <= range && std::fabs(to[1] - pos[1]) < nav::kHead;
  if (enemy && near_it) {
    const float eye[3] = {pos[0], pos[1] + 6.0f, pos[2]}, aim[3] = {to[0], to[1] + 5.0f, to[2]};
    if (!h.wall_between(eye, aim)) {
      has_spot = false;
      follow.path.clear();
      return false;  // in sight and in range: the exe's fight
    }
    const bool moved = std::hypot(to[0] - spot_for[0], to[2] - spot_for[2]) > 25.0f;
    if ((!has_spot || moved) && now - spot_t >= 1.0f) {
      spot_t = now;
      has_spot = firing_spot(pos, to, range, spot);
      if (has_spot) ++spots, spot_for[0] = to[0], spot_for[1] = to[1], spot_for[2] = to[2];
    }
    if (has_spot) goal[0] = spot[0], goal[1] = spot[1], goal[2] = spot[2];
  } else {
    has_spot = false;
    // A leader in range and in sight: the exe's follow and its band.
    if (!enemy && near_it) {
      const float eye[3] = {pos[0], pos[1] + 6.0f, pos[2]}, aim[3] = {to[0], to[1] + 5.0f, to[2]};
      if (!h.wall_between(eye, aim)) return follow.path.clear(), false;
    }
  }
  if (follow.step(pos, goal, now, dt, out, must_find, has_spot ? 0.0f : range)) return true;
  // The spot in a straight line: walked to as it is (the fallbacks head for the enemy).
  if (has_spot && now >= follow.fallback_until && std::hypot(spot[0] - pos[0], spot[2] - pos[2]) > 6.0f &&
      nav::line_clear_avoiding(nav::grid(), pos, spot)) {
    out[0] = spot[0], out[1] = pos[1], out[2] = spot[2];
    return true;
  }
  return false;
}

// Straight at the grid's waypoint, as the follow's crumbs: walking only while
// it is within 60 degrees ahead.
void nav_drive(Brain& b, zgame::Handle v, const float pos[3], const float goal[3], float* turn) {
  BrainHooks& h = brain_hooks();
  float fwd[3] = {0, 0, 0};
  h.forward(v, &fwd[0], &fwd[2]);
  float dir[3] = {goal[0] - pos[0], 0.0f, goal[2] - pos[2]};
  const float gl = std::hypot(dir[0], dir[2]);
  if (gl > 0.01f) dir[0] /= gl, dir[2] /= gl;
  float dot = 0, cross = 0;
  *turn = turn_toward(fwd, dir, &dot, &cross, *turn, turn_params());
  b.controls->set_turn(*turn);
  b.controls->set_throttle(dot >= 0.5f ? dot : 0.0f);
}

// Mech3.exe 1.2 @ 0x004d1d10. The exe takes the order's part as the vehicle
// net's (its vt+0x0c), as it always is: the brain pushes the fight on that
// net; any other part here gives up.
void FightState::run(float dt) {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  const zgame::Handle v = b.controls->vehicle();
  auto* part = dynamic_cast<VehiclePart*>(order->net.get());
  if (!part) {
    order->pop();
    return;
  }
  Behaviour* mode = part->behaviour;
  const zgame::Handle target = part->target->vehicle;
  float to[3], from[3];
  h.position(target, to);  // the net's vt+0x04 (0x004c02e0)
  h.position(v, from);     // 0x005a85e0
  if (!h.entity_active(target) || b.must_break_off(v)) {
    order->pop();
    return;
  }
  float throttle;
  // FIX (bug), ours (W27): stuck short of the enemy behind a wall, round it.
  float ahead[2] = {0, 0}, goal[3];
  if (zfix::options().bugs) {
    h.forward(v, &ahead[0], &ahead[1]);
    // Ours (W58): by the grid to a firing spot, when the straight way or the sight is blocked.
    if (nav_for(v) && nav.step(v, from, to, true, b.params.get<float>(0x28), dt, goal)) {
      drive_to(b, v, from, goal, &turn, &throttle, false);
      return;
    }
    if (detour.goal(from, ahead, to, b.params.get<float>(0x28), dt, goal)) {
      drive_to(b, v, from, goal, &turn, &throttle, false);
      return;
    }
  }
  if (drive_to(b, v, from, to, &turn, &throttle, true) && mode) order->set_behaviour(mode);  // 0x004e8120
}

namespace {
// [0x0060b4a4] = cos(10 degrees), from the start-up's 0x004ce5b0.
float cos_ten_degrees() { return static_cast<float>(std::cos(0.17453292519944444)); }
// The pilot's speed for the carrot (+0x84, or +0xec when it is 0: fcomp,
// test ah 0x40, so a NaN too) at the vehicle's vt+0x118.
float follow_speed(Brain& b, zgame::Handle v) {
  float share = b.params.get<float>(0x84);
  if (!(share < 0.0f || share > 0.0f)) share = b.xec;  // [0x005cc51c] 0
  return brain_hooks().speed_at(v, share);
}
bool same_or_nan(float a, float b) { return !(a < b || a > b); }
bool unordered_or_less(float a, float b) { return !(a >= b); }  // x87 C0 after fcomp a, b
}  // namespace

// Mech3.exe 1.2 @ 0x004d1870 (with the context 0x004cecf0)
FollowState::FollowState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) {
  BrainHooks& h = brain_hooks();
  Brain& br = *o->owner;
  const zgame::Handle v = br.controls->vehicle();
  auto* part = dynamic_cast<VehiclePart*>(o->net.get());
  if (!part || !part->target) return;
  float pos[3], to[3];
  h.position(v, pos);
  h.position(part->target->vehicle, to);  // the net's vt+0x04 (0x004c02e0)
  br.steer.set(pos, to, follow_speed(br, v), v);
  br.steer.x38 = cos_ten_degrees();
}

// The run's two resets (0x004d1a20, 0x004d1aea): the segment set again from
// `to`, the carrot where it was from the pilot, the side offset kept, the
// carrot no nearer than `floor`.
void FollowState::aim(Brain& b, zgame::Handle v, const float pos[3], const float to[3], float floor) {
  Steer& s = b.steer;
  const float along = s.along, across = s.across;
  s.set(pos, to, follow_speed(b, v), v);  // 0x004cea30
  s.x38 = cos_ten_degrees();
  s.across = across;
  s.along_at = s.along_at - along;
  s.along_min = floor;
  if (unordered_or_less(s.along_at, s.along_min)) s.along_at = s.along_min;
}

bool& follow_detour_blocked() {
  static bool blocked = false;
  return blocked;
}

namespace {
constexpr float kProbe = 25.0f;     // how far a heading must be clear
constexpr float kEye = 4.0f;        // the probes' height over the pilot's place
constexpr float kStep = 0.2617994f;  // 15 degrees between the headings tried
// The heading `k` steps of 15 degrees to `side` off the way to the target
// (x, z of a unit vector) clear of walls for kProbe from 4 m out.
bool heading_clear(const float pos[3], float a0, int32_t side, int32_t k, float out[2]) {
  const float a = a0 + static_cast<float>(side * k) * kStep;
  const float x = std::sin(a), z = std::cos(a);
  const float from[3] = {pos[0] + 4.0f * x, pos[1] + kEye, pos[2] + 4.0f * z};
  const float to[3] = {pos[0] + kProbe * x, pos[1] + kEye, pos[2] + kProbe * z};
  if (follow_detour_blocked() || brain_hooks().wall_between(from, to)) return false;
  out[0] = x, out[1] = z;
  return true;
}
// The clear heading nearest the target, turning to `side` (0: either, the
// nearer), at most 150 degrees off; the side taken.
bool pick_heading(const float pos[3], const float to[3], int32_t* side, float out[2]) {
  const float a0 = std::atan2(to[0] - pos[0], to[2] - pos[2]);
  for (int32_t k = 1; k <= 10; ++k)
    for (const int32_t s : {1, -1}) {
      if (*side && s != *side) continue;
      if (heading_clear(pos, a0, s, k, out)) return *side = s, true;
    }
  return false;
}
}  // namespace

// Ours (W17, FIX (bug) behind bugs): where to walk this frame when the
// pilot has stopped short of the target (a wall, or a contact holding the
// carrot back); false to follow as the exe.
bool WallDetour::goal(const float pos[3], const float fwd[2], const float to[3], float reach, float dt, float goal[3]) {
  WallDetour& d = *this;
  const float dx = to[0] - pos[0], dz = to[2] - pos[2];
  const float dist = std::sqrt(dx * dx + dz * dz);
  const float far = reach;  // past the far end of his follow range (+0x28): never where the follow settles
  if (d.cool > 0) d.cool -= dt;
  if (!d.on) {
    if (dist <= far || d.cool > 0) {
      d.watch_t = 0, d.watch_d = dist, d.watch_fwd[0] = fwd[0], d.watch_fwd[1] = fwd[1];
      return false;
    }
    d.watch_t += dt;
    if (d.watch_t < 2.0f) return false;
    // No progress, not turning (cos 20 degrees): a wall, or the contacts'
    // push (Steer::push_by_contacts) holding the carrot within his slack.
    const bool turned = fwd[0] * d.watch_fwd[0] + fwd[1] * d.watch_fwd[1] < 0.9396926f;
    const bool stuck = dist > d.watch_d - 2.0f && !turned;
    d.watch_t = 0, d.watch_d = dist, d.watch_fwd[0] = fwd[0], d.watch_fwd[1] = fwd[1];
    if (!stuck) return false;
    d.side = 0;
    if (!pick_heading(pos, to, &d.side, d.dir)) {
      d.cool = 10.0f, ++d.gave_up;
      return false;
    }
    d.on = true, d.time = 0, d.probe_t = 0.5f, ++d.walked;
  } else {
    d.time += dt;
    const float eye[3] = {pos[0], pos[1] + kEye, pos[2]};
    const float lead = std::min(dist - 8.0f, 40.0f) / std::max(dist, 1.0f);
    const float ahead[3] = {pos[0] + dx * lead, pos[1] + kEye, pos[2] + dz * lead};
    const bool clear = d.time > 1.0f && !brain_hooks().wall_between(eye, ahead);
    if (dist <= reach - 2.0f || clear || d.time > 20.0f) {
      if (d.time > 20.0f) d.cool = 10.0f, ++d.gave_up;  // give up: the exe's straight follow for a while
      d.on = false, d.watch_t = 0, d.watch_d = dist, d.watch_fwd[0] = fwd[0], d.watch_fwd[1] = fwd[1];
      return false;
    }
    d.probe_t -= dt;
    if (d.probe_t <= 0) {
      d.probe_t = 0.5f;
      if (!pick_heading(pos, to, &d.side, d.dir)) {
        d.on = false, d.cool = 10.0f, ++d.gave_up;
        return false;
      }
    }
  }
  goal[0] = pos[0] + d.dir[0] * 20.0f, goal[1] = pos[1], goal[2] = pos[2] + d.dir[1] * 20.0f;
  return true;
}

namespace {
constexpr float kCrumbStep = 8.0f;   // the leader's way, a point each 8 m
constexpr size_t kCrumbs = 60;       // kept
constexpr float kCrumbReached = 6.0f;
constexpr float kBody = 5.0f;        // half a mech's width: the way must be clear of walls this far each side
constexpr float kSkip = 30.0f;       // the farthest crumb taken ahead of the next
constexpr float kOut = 3.5f;         // the sight lines from this far out: past his own body
// A mech's way from a to b clear: the line from knee height to over its
// head, and its two sides kBody off it; from kOut out (the segments meet the
// mechs too: his own body), to `stop` short of b (the leader's).
bool sight(const float a[3], const float b[3], float stop = 0.0f) {
  const float dx = b[0] - a[0], dz = b[2] - a[2];
  const float len = std::hypot(dx, dz);
  if (len < kOut + stop + 1.0f) return true;
  const float ux = dx / len, uz = dz / len;
  const float s0 = kOut, s1 = len - stop;
  const float sx = uz * kBody, sz = -ux * kBody;
  const float lines[8][2] = {{0.0f, 1.5f}, {0.0f, 4.0f}, {0.0f, 6.5f}, {0.0f, 9.0f},
                             {1.0f, 2.5f}, {-1.0f, 2.5f}, {1.0f, 6.5f}, {-1.0f, 6.5f}};
  for (const auto& l : lines) {
    const float y0 = a[1] + (b[1] - a[1]) * (s0 / len), y1 = a[1] + (b[1] - a[1]) * (s1 / len);
    const float from[3] = {a[0] + ux * s0 + l[0] * sx, y0 + l[1], a[2] + uz * s0 + l[0] * sz};
    const float to[3] = {a[0] + ux * s1 + l[0] * sx, y1 + l[1], a[2] + uz * s1 + l[0] * sz};
    if (brain_hooks().wall_between(from, to)) return false;
  }
  return true;
}
}  // namespace

// Ours (W27, FIX (bug) behind bugs): the leader's place kept each 8 m.
void FollowState::trail_record(const float to[3]) {
  Trail& t = trail;
  if (!t.crumbs.empty()) {
    const auto& last = t.crumbs.back();
    const float d = std::hypot(to[0] - last[0], to[2] - last[2]);
    if (d < kCrumbStep) return;
    if (d > 4.0f * kCrumbStep) t.crumbs.clear(), t.at = -1;  // a jump (a teleport): no way walked to it
  }
  t.crumbs.push_back({to[0], to[1], to[2]});
  if (t.crumbs.size() > kCrumbs) {
    t.crumbs.pop_front();
    if (t.at >= 0) --t.at;
  }
}

// Ours (W27, FIX (bug) behind bugs): where to walk this frame when the
// leader is out of reach past the follow range: out of sight (a mech's
// width), or in sight with no way on for 3 s. His way, crumb by crumb: the
// nearest one in sight first, then on, skipping ahead only to one in sight
// within 30 m. A crumb not neared for 3 s: back one, and no skipping for 6 s.
// False to follow as the exe (or the W17 detour).
bool FollowState::trail_goal(const float pos[3], const float to[3], float reach, float dt, float goal[3]) {
  Trail& t = trail;
  const float dist = std::hypot(to[0] - pos[0], to[2] - pos[2]);
  if (dist <= reach) return t.at = -1, t.sees = true, t.force_t = 0, t.watch_t = 0, false;
  if (t.cap_t > 0) t.cap_t -= dt;
  if (t.force_t > 0) t.force_t -= dt;
  t.look_t -= dt;
  const bool look = t.look_t <= 0;
  if (look) t.look_t = 0.25f, t.sees = sight(pos, to, 6.0f);
  const int32_t n = static_cast<int32_t>(t.crumbs.size());
  if (t.at >= n) t.at = -1;
  auto far_to = [&](int32_t i) { return std::hypot(t.crumbs[i][0] - pos[0], t.crumbs[i][2] - pos[2]); };
  // Progress toward what he walks to: the crumb, or the leader.
  const float d_aim = t.at >= 0 ? far_to(t.at) : dist;
  t.watch_t += dt;
  if (t.watch_t >= 3.0f) {
    if (d_aim > t.watch_d - 2.0f) {
      if (t.at >= 0) t.at = std::max(t.at - 1, 0), t.cap_t = 6.0f;  // back one, then step by step
      else if (t.sees) t.force_t = 6.0f;                           // in sight, no way on
    }
    t.watch_t = 0, t.watch_d = t.at >= 0 ? far_to(t.at) : dist;
  }
  if (t.sees && t.force_t <= 0) return t.at = -1, false;
  if (t.at < 0) {  // the nearest crumb in sight
    std::vector<std::pair<float, int32_t>> near;
    for (int32_t i = 0; i < n; ++i) near.push_back({far_to(i), i});
    std::sort(near.begin(), near.end());
    for (const auto& c : near)
      if (sight(pos, t.crumbs[c.second].data())) {
        t.at = c.second, ++t.used, t.watch_t = 0, t.watch_d = c.first;
        break;
      }
    if (t.at < 0) return false;
  } else if (far_to(t.at) < kCrumbReached || (look && t.cap_t <= 0)) {
    const bool reached = far_to(t.at) < kCrumbReached;
    int32_t next = -1;
    if (t.cap_t <= 0)
      for (int32_t i = n - 1; i > t.at; --i)
        if (far_to(i) < kSkip && sight(pos, t.crumbs[i].data())) {
          next = i;
          break;
        }
    if (next < 0 && reached && t.at + 1 < n) next = t.at + 1;
    if (next >= 0) t.at = next, ++t.used, t.watch_t = 0, t.watch_d = far_to(next);
  }
  goal[0] = t.crumbs[t.at][0], goal[1] = t.crumbs[t.at][1], goal[2] = t.crumbs[t.at][2];
  return true;
}

// Mech3.exe 1.2 @ 0x004c9750. Without a part or target the exe reads
// through null; the port leaves the directions 0.
CircleState::CircleState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) {
  BrainHooks& h = brain_hooks();
  Brain& br = *o->owner;
  const zgame::Handle v = br.controls->vehicle();
  auto* part = dynamic_cast<VehiclePart*>(o->net.get());
  if (!part || !part->target) return;
  float to[3], pos[3];
  h.position(part->target->vehicle, to);  // the net's vt+0x04
  h.position(v, pos);                     // 0x005a85e0
  share = br.params.get<float>(0x94);
  flip = 0;
  zmth::Vec3 r{to[0] - pos[0], 0.0f, to[2] - pos[2]};
  zmth::normalize(r);  // 0x004020d0
  radial[0] = r.x, radial[1] = r.y, radial[2] = r.z;
  tangent[0] = r.z, tangent[1] = 0, tangent[2] = -r.x;
}

// Mech3.exe 1.2 @ 0x004c9880 (docs/ai.md 17.2). The radius is the brain's
// +0x44, the way round turns past +0x48 along the tangent; the look-ahead
// (the stopping distance at the share, x3, at most 10) pulled by the
// boundary turns it too.
void CircleState::run(float dt) {
  (void)dt;
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  auto* part = dynamic_cast<VehiclePart*>(order->net.get());
  if (!part || !part->target) {
    order->pop();
    return;
  }
  const zgame::Handle target = part->target->vehicle;
  float to[3], from[3];
  h.position(target, to);  // the net's vt+0x04
  h.position(v, from);     // 0x005a85e0
  if (!h.entity_active(target) || b.must_break_off(v)) {  // +0x08, 0x004e8dd0
    order->pop();  // order vt+0x08
    return;
  }
  FlatWay w;
  flat_way(from, to, &w);  // 0x004ce230
  const float R = b.params.get<float>(0x44);
  const float r = (w.dir[0] * radial[0] + w.dir[1] * radial[1]) + w.dir[2] * radial[2];
  const float e = (r * w.dist - R) / R;
  const float t = (w.dir[0] * tangent[0] + w.dir[1] * tangent[1]) + w.dir[2] * tangent[2];
  const float along = t * w.dist;
  const float P = b.params.get<float>(0x48);
  if (flip) {
    if (along > P) flip = 0;
  } else if (!(along >= -P)) {  // C0: below, or unordered
    flip = 1;
  }
  const float thr = c.throttle_now();  // IControls vt+0x1c
  float s = e;
  if (flip) {
    if (!(thr >= 0.1)) s = -e;  // 0x005cb0f0
  } else if (!(thr > -0.1)) {   // 0x005cb0f8
    s = -e;
  }
  zmth::Vec3 d{s * radial[0] + tangent[0], 0.0f, radial[2] * s + tangent[2]};
  zmth::normalize(d);  // 0x004020d0
  const float dir[3] = {d.x, d.y, d.z};
  float fwd[3] = {0, 0, 0};
  h.forward(v, &fwd[0], &fwd[2]);  // +0x24
  float dot, cross;
  turn = turn_toward(fwd, dir, &dot, &cross, turn, turn_params());  // 0x004cdd40, inline
  c.set_turn(turn);  // IControls vt+0x28
  float ahead = h.stopping_distance(v, h.speed_at(v, share)) * 3.0f;  // vt+0x128
  if (!(10.0f >= ahead)) ahead = 10.0f;
  if (flip) ahead = -ahead;
  const float step[3] = {ahead * dir[0], ahead * dir[1], ahead * dir[2]};
  float look[3] = {from[0] + step[0], from[1] + step[1], from[2] + step[2]};
  if (h.inout_clamp(v, look, look)) flip = flip == 0;  // 0x004c3000
  if (!(dot >= 0.5)) {  // 0x005cb108
    c.set_throttle(0.0f);
    return;
  }
  c.set_throttle(flip ? -share : share);  // IControls vt+0x20
}

// Mech3.exe 1.2 @ 0x004d1320
OrbitState::OrbitState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) {
  Brain& br = *o->owner;
  share = br.params.get<float>(0x94);
  if (share > br.xec) share = br.xec;  // test ah 0x41: only when above
}

// Mech3.exe 1.2 @ 0x004d13c0 with its context 0x004d12a0 (docs/ai.md 17.2).
// The point R ahead along the way it computes is not used.
void OrbitState::run(float dt) {
  (void)dt;
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  auto* part = dynamic_cast<VehiclePart*>(order->net.get());
  if (!part || !part->target) {
    order->pop();
    return;
  }
  const zgame::Handle target = part->target->vehicle;
  float from[3], to[3];
  h.position(v, from);       // the context's 0x005a85e0
  h.position(target, to);    // the target net's vt+0x04
  if (!h.entity_active(target) || b.must_break_off(v)) {  // its vehicle's +0x08, 0x004e8dd0
    order->pop();  // order vt+0x08
    return;
  }
  zmth::Vec3 u{to[0] - from[0], 0.0f, to[2] - from[2]};
  const float dist = zmth::normalize(u);  // 0x004020d0
  const float R = b.params.get<float>(0x44);
  const float f = (dist - R) / R;
  zmth::Vec3 d{u.z * way + f * u.x, 0.0f, f * u.z - u.x * way};
  zmth::normalize(d);
  const float dir[3] = {d.x, d.y, d.z};
  float ahead = h.stopping_distance(v, h.speed_at(v, share)) * 3.0f;  // vt+0x128, 0x005cc5ac
  if (!(ahead >= 10.0f)) ahead = 10.0f;  // 0x005cc5b0: at least
  const float step[3] = {ahead * dir[0], ahead * dir[1], ahead * dir[2]};
  float look[3] = {from[0] + step[0], from[1] + step[1], from[2] + step[2]};
  const bool pulled = h.inout_clamp(v, look, look);  // 0x004c3000
  if (pulled) way = -way;
  float fwd[3] = {0, 0, 0};
  h.forward(v, &fwd[0], &fwd[2]);  // the context's +0x24
  float dot, cross;
  turn = turn_toward(fwd, dir, &dot, &cross, turn, turn_params());  // 0x004cdd40
  c.set_throttle(pulled || !(dot >= 0.5) ? 0.0f : share);  // IControls vt+0x20, 0x005cc5b8
  c.set_turn(turn);  // vt+0x28
  x08 = 1;
}

// Mech3.exe 1.2 @ 0x004d1650
VeerState::VeerState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) {
  Brain& br = *o->owner;
  share = br.params.get<float>(0x94);
  if (share > br.xec) share = br.xec;
}

// Mech3.exe 1.2 @ 0x004d16f0: mode 2's run (0x004d13c0) without the look-ahead.
void VeerState::run(float dt) {
  (void)dt;
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  auto* part = dynamic_cast<VehiclePart*>(order->net.get());
  if (!part || !part->target) {
    order->pop();
    return;
  }
  const zgame::Handle target = part->target->vehicle;
  float from[3], to[3];
  h.position(target, to);  // the target net's vt+0x04 first
  h.position(v, from);     // 0x005a85e0
  if (!h.entity_active(target) || b.must_break_off(v)) {
    order->pop();
    return;
  }
  FlatWay w;
  flat_way(from, to, &w);  // 0x004ce230
  const float R = b.params.get<float>(0x44);
  const float f = (w.dist - R) / R;
  zmth::Vec3 d{f * w.dir[0] + w.dir[2] * way, 0.0f, f * w.dir[2] - w.dir[0] * way};
  zmth::normalize(d);
  const float dir[3] = {d.x, d.y, d.z};
  float fwd[3] = {0, 0, 0};
  h.forward(v, &fwd[0], &fwd[2]);  // the vehicle's +0x24
  float dot, cross;
  turn = turn_toward(fwd, dir, &dot, &cross, turn, turn_params());  // 0x004cdd40
  c.set_throttle(!(dot >= 0.5) ? 0.0f : share);  // IControls vt+0x20, 0x005cc5b8
  c.set_turn(turn);  // vt+0x28
}

// Mech3.exe 1.2 @ 0x004d1980
void FollowState::run(float dt) {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  auto* part = dynamic_cast<VehiclePart*>(order->net.get());
  if (!part || !part->target) {
    order->pop();
    return;
  }
  const zgame::Handle target = part->target->vehicle;
  float to[3], pos[3], fwd[3] = {0, 0, 0};
  h.position(v, pos);      // the context's (0x004cecf0)
  h.position(target, to);  // the net's vt+0x04 (0x004c02e0)
  if (!h.entity_active(target)) {  // +0x08
    order->pop();  // order vt+0x08
    return;
  }
  float slack_along = 0, slack_side = 0;
  bool on_crumb = false;
  float at[3] = {to[0], to[1], to[2]};
  float goal[3], ahead[2] = {0, 0};
  if (zfix::options().bugs) h.forward(v, &ahead[0], &ahead[1]), trail_record(to);  // ours, for the detour and the trail
  const bool clamped = h.inout_clamp(v, to, at);  // 0x004c3000: the boundary pulls the target's place
  // FIX (bug), ours (W58): a lancemate by the grid when the exe's walk fails:
  // a wall on the way, or no progress, the boundary's pull included (the leader
  // stands in a shape: it holds him at the edge).
  const bool at_edge = clamped && !nav.follow.path.empty();  // keeps to a path it took
  if (zfix::options().bugs && nav_for(v) &&
      nav.step(v, pos, to, !h.targetable(target) || !h.targetable(target)->is_ally(v), b.params.get<float>(0x28), dt, goal, at_edge)) {
    aim(b, v, pos, goal, 0.0f);
    slack_side = 2.0f;
    pulled = 0;
    detour.on = false, detour.watch_t = 0;
    on_crumb = true;
  } else if (clamped) {
    const Steer& s = b.steer;
    if (!(pulled && same_or_nan(at[0], s.start[0]) && same_or_nan(at[2], s.start[2]))) aim(b, v, pos, at, 0.0f);
    pulled = 1;
    detour.on = false;
  } else if (zfix::options().bugs && trail_goal(pos, to, b.params.get<float>(0x28), dt, goal)) {
    aim(b, v, pos, goal, 0.0f);  // FIX (bug), ours (W27): along the leader's way
    slack_side = 2.0f;
    pulled = 0;
    detour.on = false, detour.watch_t = 0;
    on_crumb = true;
  } else if (zfix::options().bugs && detour.goal(pos, ahead, to, b.params.get<float>(0x28), dt, goal)) {
    aim(b, v, pos, goal, 0.0f);  // FIX (bug), ours (W17): round the wall
    slack_side = 2.0f;
    pulled = 0;
  } else {
    const float p28 = b.params.get<float>(0x28), p2c = b.params.get<float>(0x2c);
    aim(b, v, pos, to, (p2c + p28) * 0.5f);  // 0x005cc564
    slack_along = (p28 - p2c) * 0.5f;
    slack_side = 2.0f;  // 0x005e8be0
    pulled = 0;
  }
  Steer& s = b.steer;
  s.measure(v, dt);  // 0x004ceb50
  const float away[2] = {std::fabs(s.along), std::fabs(s.side)};  // 0x004cee50
  const float sum = away[0] + away[1];
  h.forward(v, &fwd[0], &fwd[2]);
  float turn = 0, throttle = 0;
  walk_drive(b, v, pos, fwd, away, sum, 2.0f, &turn, &throttle, 0.05f, slack_along, slack_side);  // 0x005e8be4, 0x005e8be8
  if (on_crumb) {
    // Ours (W27): straight at the crumb, as the veer state steers (the
    // carrot's slack and the contacts' push would swing him round a near
    // point); walking only while it is within 60 degrees ahead.
    float dir[3] = {goal[0] - pos[0], 0.0f, goal[2] - pos[2]};
    const float gl = std::hypot(dir[0], dir[2]);
    if (gl > 0.01f) {
      dir[0] /= gl, dir[2] /= gl;
      float dot = 0, cross = 0;
      crumb_turn = turn_toward(fwd, dir, &dot, &cross, crumb_turn, turn_params());
      turn = crumb_turn;
      throttle = dot >= 0.5f ? std::max(throttle, dot) : 0.0f;
    }
  }
  c.set_turn(turn);          // IControls vt+0x28
  c.set_throttle(throttle);  // vt+0x20
}

// Mech3.exe 1.2 @ 0x004cc150. The other is the vehicle's bearer (its vt+0x2c
// then vt+0x40: a mech itself).
BehaviourState* make_station_state(Behaviour& b, Order& o) {
  BrainHooks& h = brain_hooks();
  Brain* br = o.owner;
  const zgame::Handle v = br && br->controls ? br->controls->vehicle() : 0;
  if (!v || !o.net) return nullptr;
  auto* vn = dynamic_cast<VehicleNet*>(o.net->net());  // vt+0x08, then the net's vt+0x58
  if (!vn || !vn->vehicle) return nullptr;
  const zgame::Handle other = h.bearer(vn->vehicle);
  if (h.vehicle_xf4(v)) {
    if (other && h.vehicle_xf4(other)) return nullptr;
    return new StationHoldState(&o, &b);  // 0x004cc240
  }
  // FIX (bug): the exe reads a missing other's +0xbc (a crash); here none.
  if (h.vehicle_flags(v) & 0x10) {
    if (!other || (h.vehicle_flags(other) & 0x10) || h.player_vehicle() == other) return nullptr;
  }
  return new StationState(&o, &b);  // 0x004cc290
}

// Mech3.exe 1.2 @ 0x004cc290 (over 0x004c8a30)
StationState::StationState(Order* o, Behaviour* b) : BehaviourState(o, b), made_by(b) { take_params(); }

void StationState::take_params() {
  Brain& br = *order->owner;
  way = br.controls->reversing() ? -1.0f : 1.0f;  // IControls vt+0x18
  close = br.params.get<float>(0x78) * 0.9f;  // 0x005cb0d8
  range = std::sqrt(br.params.get<float>(0x74)) * 1.1f;  // 0x005cb140, 1.1f as a double
}

// Mech3.exe 1.2 @ 0x004cc380
void StationState::resume() { take_params(); }

namespace {
// The turn toward `to` (flat) the walking states' way, `keep` straight back
// (1 for none): the run's three inlined copies (0x004cc80b, 0x004cc8f3,
// 0x004cca33).
float turn_onto(const float fwd[3], const float to_x, const float to_z, float dot, float keep) {
  const TurnParams& p = turn_params();
  if (dot < -p.cos_dead) return keep == 0.0f || std::isnan(keep) ? 1.0f : keep;
  if (dot >= p.cos_dead) return 0.0f;  // a NaN turns
  const float cross = fwd[2] * to_x - fwd[0] * to_z;
  return turn_past(cross, p.sin_dead, p.gain, p.least, dot);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004cc3e0 (docs/ai.md 17.2). Phases 1..3 fall back to 0
// in the same run when they no longer hold.
void StationState::run(float) {
  BrainHooks& h = brain_hooks();
  Brain& br = *order->owner;
  BrainControls& c = *br.controls;
  const zgame::Handle v = c.vehicle();
  auto* vn = order->net ? dynamic_cast<VehicleNet*>(order->net->net()) : nullptr;  // vt+0x0c, vt+0x08, vt+0x58
  const zgame::Handle target = vn ? vn->vehicle : 0;
  bool done = false, idle = false;
  if (!target || !h.entity_active(target)) {  // +0x08
    order->pop();
    return;
  }
  float tp[3], vp[3];
  h.position(target, tp);  // 0x005a85e0
  h.position(v, vp);
  FlatWay w;
  flat_way(vp, tp, &w);   // 0x004ce230
  float fwd[3] = {};
  h.forward(v, &fwd[0], &fwd[2]);  // +0x24
  const zgame::Handle other = h.bearer(target);  // vt+0x2c, vt+0x40
  float ov[3], tv[3];
  h.velocity(v, ov);  // vt+0x08
  (void)std::fabs(h.move_speed(v));  // vt+0x114: read, not used
  if (other) {
    h.velocity(other, tv);
    (void)std::fabs(h.move_speed(other));
  } else {
    tv[0] = tv[1] = tv[2] = 0;
  }
  zmth::Vec3 rel{ov[0] - tv[0], ov[1] - tv[1], ov[2] - tv[2]};
  const float rel_speed = zmth::normalize(rel);  // 0x004020d0
  const float* d = w.dir;
  const float dist = w.dist;
  const float closing = (rel.x * d[0] + rel.y * d[1]) + rel.z * d[2];
  const float side = d[0] * rel.z - d[2] * rel.x;
  const float ahead = (fwd[0] * d[0] + fwd[1] * d[1]) + fwd[2] * d[2];
  const float p78 = br.params.get<float>(0x78);
  bool again;
  int zeros = 0;
  do {
    again = false;
    switch (phase) {
      case 0:
        // FIX (bug): phase 0 again in one run decides as before, so the exe
        // loops for ever (a hang) when +0x78 is below the +0x24 it took
        // (0.9 of it: a negative +0x78, none shipped); here the run stops.
        if (++zeros > 1) break;
        if (dist > range) {  // past +0x28: the order ends
          done = true;
          count = 0;
        } else if (!(dist >= close)) {  // too close (a NaN too)
          if (ahead > 0.5f) phase = 1, way = -1.0f;        // 0x005e8b94
          else if (!(ahead >= -0.5f)) phase = 2, way = 1.0f;   // 0x005e8b98 (a NaN too)
          else phase = 3, way = !(way >= 0.0f) ? -1.0f : 1.0f;
          again = true;
          count = 0;
        } else if (rel_speed > 0.2f && closing > 0.0f && !(closing * dist > p78)) {  // 0x005cb138
          turn = 0;  // closing in: swerve, 8 frames
          if (side < -0.17f) turn = 1.0f;       // -[0x005e8b9c], 0x005e8ba0
          else if (side > 0.17f) turn = -1.0f;
          way = c.reversing() ? -1.0f : 1.0f;   // IControls vt+0x18
          count = 8;
        } else if (count != 0) {
          turn = 0;
          way = c.reversing() ? -1.0f : 1.0f;
          --count;
        } else {
          idle = true;  // the controls left alone
        }
        break;
      case 1:  // the other ahead: back off facing it
        if (!(ahead >= 0.0f) || dist > p78) {
          phase = 0, again = true;
          break;
        }
        turn = turn_onto(fwd, d[0], d[2], fwd[0] * d[0] + fwd[2] * d[2], turn);
        break;
      case 2: {  // behind: on, its back to it
        if (ahead > 0.0f || dist > p78) {
          phase = 0, again = true;
          break;
        }
        const float back[3] = {-fwd[0], -fwd[1], -fwd[2]};
        turn = turn_onto(back, d[0], d[2], back[0] * d[0] + back[2] * d[2], turn);
        break;
      }
      case 3: {  // beside: alongside, across the way to it
        if (!(ahead >= -0.8) || ahead > 0.8 || dist > p78) {  // 0x005cb150, 0x005cb158: doubles
          phase = 0, again = true;
          break;
        }
        float px = d[2], pz = -d[0];
        const float g = fwd[0] * d[0] + fwd[2] * d[2];
        float g2 = g;
        if (!(g >= 0.0f)) px = -px, pz = -pz, g2 = -g;
        turn = turn_onto(fwd, px, pz, g2, turn);
        break;
      }
      default: break;
    }
  } while (again);
  if (done) {
    order->pop();  // 0x004e8270 (the own order's)
    return;
  }
  if (idle) return;
  c.set_turn(turn);   // IControls vt+0x28
  c.set_throttle(way);  // vt+0x20
}

// Mech3.exe 1.2 @ 0x004cc020. The other vehicle through the order's part
// (the vehicle net's, as always here).
void StationHoldState::run(float) {
  BrainHooks& h = brain_hooks();
  Brain& br = *order->owner;
  auto* vn = order->net ? dynamic_cast<VehicleNet*>(order->net->net()) : nullptr;
  const zgame::Handle other = vn ? vn->vehicle : 0;
  if (other) {
    float a[3], t[3];
    h.position(br.controls->vehicle(), a);  // 0x005a85e0
    h.position(other, t);
    const float dx = t[0] - a[0], dy = t[1] - a[1], dz = t[2] - a[2];
    const float d = std::sqrt((dx * dx + dy * dy) + dz * dz);
    if (!(d >= range)) {  // within (a NaN too)
      // [0x0060b48c] (never set): the pilot stopped.
      phase = 1;
      return;
    }
  }
  order->pop();
}

// Mech3.exe 1.2 @ 0x004e9ca0
bool attack(Brain& b, AiNet* n) {
  b.remake(n);  // 0x004e82e0
  auto* an = new AttackNet();
  brain_hooks().on_new(0x1c, an);
  AttackEnd end(an);
  AttackRearm rearm;
  AttackReturn home(n);
  b.push(an, &end);   // 0x004e87c0
  b.push(an, &rearm);
  b.push(an, &home);
  return true;
}

}  // namespace mech3::ai

namespace mech3::ai {

// --- the combat manoeuvre (docs/ai.md 17.2) ------------------------------------

// Mech3.exe 1.2 @ 0x004caae0 over 0x004ca440 (the base's part first: the
// jets, the count at +0xf0, the stop; then the time).
ManoeuvreState::ManoeuvreState(Order* o, Behaviour* b, bool given) : BehaviourState(o, b) {
  BrainHooks& h = brain_hooks();
  Brain& br = *o->owner;
  bool none = given;
  if (!none) {
    const zgame::Handle v = br.controls->vehicle();
    Pilot p;
    if (v) h.pilot_of(v, &p);  // vt+0x50's +0x3d8
    none = !v || !(p.jump_jets > 0);
  }
  ++br.xf0;
  no_jets = none ? 1 : 0;
  jump = none ? 0.0f : 1.0f;
  br.controls->stop();  // IControls vt+0x14
  const float now = h.now(), lo = br.params.get<float>(0x6c), hi = br.params.get<float>(0x70);
  until = (hi - lo) * 3.051851e-05f * static_cast<float>(h.rand()) + lo + now;  // 0x005cb128
}

// Mech3.exe 1.2 @ 0x004ca570
ManoeuvreState::~ManoeuvreState() {
  Brain& br = *order->owner;
  br.controls->stop();
  --br.xf0;
}

// Mech3.exe 1.2 @ 0x004ca5f0: this state gone for 0x0060b450's.
void ManoeuvreState::suspend() { order->set_behaviour(after_manoeuvre_behaviour()); }

// Mech3.exe 1.2 @ 0x004cab80: the one at 0x00604088 for the 0x00604098 one too.
Behaviour* ManoeuvreState::behaviour() const { return brain_hooks().manoeuvre_behaviour(1); }

namespace {
// 0x004ca7c4..0x004ca900: 0x004c8df0's turn inlined, the x87's compares
// as written: a NaN dot (no way to the target) counts as below on each
// test, so the turn is full to a side.
float manoeuvre_turn(const float fwd[3], const float dir[3], float keep) {
  const TurnParams& p = turn_params();
  const float dot = fwd[0] * dir[0] + fwd[2] * dir[2];
  if (dot < -p.cos_dead) return keep == 0.0f || std::isnan(keep) ? 1.0f : keep;  // C3: equal or unordered
  if (dot >= p.cos_dead) return 0.0f;
  const float cross = fwd[2] * dir[0] - dir[2] * fwd[0];
  if (!(dot >= 0.0f)) return !(cross >= 0.0f) ? -1.0f : 1.0f;
  if (cross < -p.sin_dead) {
    const float k = (p.sin_dead + cross) * p.gain;
    if (k < -1.0) return -1.0f;
    const float nl = -p.least;
    return k > nl ? nl : k;
  }
  if (!(cross > p.sin_dead)) return 0.0f;
  const float k = (cross - p.sin_dead) * p.gain;
  if (k > 1.0) return 1.0f;
  return k < p.least ? p.least : k;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ca600. The throttle brakes against the speed (the
// forward speed negated while airborne, the jets' thrust then); the phases
// run on in one call while each hands over.
void ManoeuvreState::run(float) {
  BrainHooks& h = brain_hooks();
  Brain& b = *order->owner;
  BrainControls& c = *b.controls;
  const zgame::Handle v = c.vehicle();
  float pos[3];
  h.position(v, pos);  // 0x005a85e0
  float steer = 0;
  if (!no_jets) {
    const float fuel = h.jump_fuel(v);       // vt+0x148
    if (!(double(fuel) >= 0.1)) jump = 0;    // 0x005cb0f0
    else if (fuel > 0.6f) jump = 1;          // 0x005cb110
  }
  if (!(h.now() < until)) {
    order->pop();  // order vt+0x08
    return;
  }
  const float s = h.airborne(v) ? -h.forward_speed(v) : h.move_speed(v);  // vt+0x144: +0x44, else vt+0x114
  float thr;
  if (!(s >= -1.0f)) {
    const float k = (s + 1.0f) * 0.2f;  // 0x005cb8dc
    thr = !(k >= -1.0f) ? 1.0f : -k;
  } else if (!(s > 1.0f)) {
    thr = 0;
  } else {
    float k = (s - 1.0f) * 0.2f;
    if (k > 1.0f) k = 1.0f;
    thr = -k;
  }
  for (bool again = true; again;) {
    again = false;
    if (phase == 0) {
      if (!(double(thr) < -0.0001) && double(thr) <= 0.0001) {  // 0x005cb118, 0x005cb120
        phase = 1;
        again = true;
      }
    } else if (phase == 1) {
      if (!target) {
        if (const Contact* k = h.pick(v, 1)) target = k->seen;  // 0x004d6b90: an enemy
        if (!target && c.hit_lately()) target = c.attacker();   // 0x004aec80 on the hit record
        if (!target)
          if (const Contact* k = h.pick(v, 3)) target = k->seen;  // anyone
      }
      if (target) {
        again = true;
        b.xf8 = 1;
        steer = turn;  // phase 2 sets it again in this call
        ++phase;
      }
    } else if (phase == 2) {
      float fwd[3] = {0, 0, 0}, at[3];
      h.forward(v, &fwd[0], &fwd[2]);  // +0x24
      if (!h.lead_point(v, target, at)) h.entity_lock_position(target, at);  // 0x005a8630
      FlatWay w;
      flat_way(pos, at, &w);  // 0x004ce230
      turn = manoeuvre_turn(fwd, w.dir, turn);
      steer = turn;
      if ((b.params.raw[0xcc] & 0x40) && !b.xf8) until = 0;
    }
  }
  c.set_jump(pos[1] < b.params.get<float>(0x7c) ? jump : 0.0f);  // IControls vt+0x34: below the brain's +0x7c
  if (h.airborne(v))
    c.set_jet_forward(thr);  // vt+0x3c
  else
    c.set_throttle(thr);  // vt+0x20
  c.set_turn(steer);  // vt+0x28
}

// Mech3.exe 1.2 @ 0x004ca420
void AfterManoeuvreState::run(float) {
  order->owner->x104 = 1;  // run the order again
  order->pop();
}

// Mech3.exe 1.2 @ 0x004ca3d0
Behaviour* AfterManoeuvreState::behaviour() const { return after_manoeuvre_behaviour(); }

Behaviour* after_manoeuvre_behaviour() {
  static Behaviour b(StepKind::AfterManoeuvre);  // 0x0060b450 (0x004ca330)
  return &b;
}

}  // namespace mech3::ai
