// The parts' setups and updates: the torso's aim and shake, the hip's crouch,
// the feet on the ground, the arms' aim and recoil (docs/movement-ground.md,
// The part updates). Original file: mech3\mech_parts.cpp (0x0045fdd0..0x00466f00).
#include <cmath>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "mech3/mech.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_move.h"
#include "mech3/mech_reader.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
PartCalls g_calls;

constexpr float kCrouchRate = -2.0f;       // 0x005c27c8, per second
constexpr float kCrouchDefault = 0.7f;     // 0x005c27c4, of the hip's height
constexpr float kTouch = 0.25f;            // 0x005e4f0c: a falling foot this near touches
constexpr float kLift = 0.2f;              // 0x005e4f10: a planted foot this high is in the air
constexpr float kShadowLift = -0.1f;       // 0x005e4f08
constexpr float kSpinMin = 0.01f;          // 0x005e4f18
constexpr float kSpinDecay = -3.0f;        // 0x005c27e0, per second
constexpr float kStepVolume = 0.45f;       // 0x005e4ec0: the step sound at a standstill
constexpr float kPrintLift = -0.05f;       // 0x005c27e8, taken off: footprints sit 5 cm up
constexpr float kStepForce = 1000.0f;      // 0x447a0000: what a foot hands what it steps on
constexpr float kAimDecay = -2.0f;         // 0x005e4f38, per second (the AI's aim)
constexpr double kAimFloor = 0.03;         // 0x005c27f8
constexpr float kLevel = 1.75f;            // 0x005e4f30 / 0x005e4f50: the pitch lock, the absolute rate
constexpr float kCentre = 1.4f;            // 0x005e4f54: the absolute rate while centring
constexpr float kSlow = 0.3f;              // 0x005e4f2c / 0x005e4f4c
constexpr double kSnap = 0.05000000074505806;  // 0x005c27d0
constexpr double kNear = 0.009999999776482582;  // 0x005c2800
constexpr float kShakeStep = 0.06666667f;  // 0x005c2808: the shake spring in 1/15 s substeps
constexpr float kServoVolume = 0.6f;       // 0x3f19999a
constexpr int kServoSound = 4;
constexpr int kDuckSound = 0xc, kDuckSlot = 12;  // mech +0x5e8
constexpr float kRecoilHold = 4.0f;        // 0x005c2820, per second
constexpr float kRecoilDecay = 6.0f;       // 0x005c2824
constexpr float kRecoilMin = 0.001f;       // 0x005c2828

zmth::Quat quat(const float q[4]) { return {q[0], q[1], q[2], q[3]}; }
void store(float* d, const zmth::Quat& q) { d[0] = q.w, d[1] = q.x, d[2] = q.y, d[3] = q.z; }
zmth::Vec3 v3(const Vec3& v) { return {v.x, v.y, v.z}; }

int world_pos(Handle node, Vec3* out) {
  if (g_calls.world_position) return g_calls.world_position(node, out);
  return zcls::node_world_position(static_cast<zcls::Node*>(node), &out->x);
}
int to_world(Handle node, int mode) {
  if (g_calls.node_to_world) return g_calls.node_to_world(node, mode);
  return zcls::node_to_world(static_cast<zcls::Node*>(node), mode);
}
const MechDfn* dfn(Mech& m) { return g_calls.dfn ? g_calls.dfn(m) : mech_dfn(m.pilot->roster); }
// 0x0041e9a0, the mech's own lookup (the torso's limits)
const MechDfn* mech_def(Mech& m) {
  return update_calls().dfn ? update_calls().dfn(m) : mech_dfn(m.pilot->roster);
}
void* screen(Mech& m) {
  return &m == world().player && heat_calls().screen ? heat_calls().screen(m) : nullptr;
}
// The HUD's +0x1514 bit 4 clear (the reticule zoomed in, controls.md): the
// player's torso turns at 0.3 the rate.
bool slow_turn(Mech& m) {
  void* s = screen(m);
  return s && g_calls.screen_flags && !(g_calls.screen_flags(s) & 0x10);
}
bool playing(Handle e) { return g_calls.effect_playing && g_calls.effect_playing(e); }
void stop_fx(Handle e) {
  if (damage_calls().stop_foot_effect) damage_calls().stop_foot_effect(e);
}
Vec3 position(Mech& m) {  // 0x005a85e0
  if (skis_calls().position) return *skis_calls().position(m);
  return m.transform ? Vec3{m.transform[9], m.transform[10], m.transform[11]} : Vec3{0, 0, 0};
}

// p through a 3x4 (row vectors), the x87's order of the sums.
Vec3 through(const float* m, const Vec3& p) {
  return {((p.x * m[0] + p.y * m[3]) + p.z * m[6]) + m[9], ((p.x * m[1] + p.y * m[4]) + p.z * m[7]) + m[10],
          ((p.x * m[2] + p.y * m[5]) + p.z * m[8]) + m[11]};
}

// MSVCRT _CIasin under the game's x87 settings: atan2(x, sqrt(1 - x^2)).
float crt_asin(float x) {
  const float s = std::sqrt(1.0f - x * x);
  return static_cast<float>(std::atan2(double{x}, double{s}));
}

// x then y onto a quaternion, normalised (0x005251a0, 0x00525130, 0x005255d0, 0x00525500).
void yaw_pitch(float yaw, float pitch, float* out) {
  zmth::Quat a, b, c, n;
  zmth::quat_yaw(yaw, a);
  zmth::quat_pitch(pitch, b);
  zmth::quat_mul(a, b, c);
  zmth::quat_normalize(c, n);
  store(out, n);
}

// (target - v) * rate * dt + v, snapped within 0.01.
void approach(float* v, float target, float rate, float dt) {
  const float n = (target - *v) * rate * dt + *v;
  *v = n;
  if (std::fabs(target - n) < kNear) *v = target;
}
}  // namespace

PartCalls& part_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x0045fdd0: a part with a mech and a node joins it.
bool MechPart::setup() {
  if (!mech || !node) return false;
  if (g_calls.register_part) g_calls.register_part(*mech, *this);  // 0x0041b8d0
  if (g_calls.init) g_calls.init(*this);                           // vtable +0x08 (0): the torso's side structure
  else init(nullptr);
  return true;
}

// Mech3.exe 1.2 @ 0x00461730. Under water when both feet are in it and the
// part is below the left foot's surface; the player's HUD redraws its weapon
// list when that changes.
void MechPart::update(float) {
  Mech& m = *mech;
  bool under = false;
  if (m.foot(0)->in_water && m.foot(1)->in_water) {
    const float surface = m.foot(0)->water_surface;
    Vec3 w{0, 0, 0};
    world_pos(node, &w);
    under = !(w.y >= surface);
  }
  void* s = screen(m);
  if (s && under != submerged && damage_calls().hud_weapons) damage_calls().hud_weapons(s);
  submerged = under;
}

// Mech3.exe 1.2 @ 0x00462130: the hip's rest height and the crouch's depth
// (def crouch_height, 0.7 of the hip's height when 0), the thighs' spacing.
bool Hip::setup() {
  const bool ok = MechPart::setup();
  if (!ok) return ok;
  Mech& m = *mech;
  m.hip_height = translation->y;
  const float c = dfn(m)->get<float>(0x148);
  m.crouch_depth = (c < 0.0f || c > 0.0f) ? c : m.hip_height * kCrouchDefault;
  track = std::fabs(m.legs[0]->translation->x - m.legs[1]->translation->x);
  return ok;
}

// Mech3.exe 1.2 @ 0x004637b0: the IK's constants from the rest pose. The
// foot (a child of the hip) is taken into the thigh's and the calf's frames;
// the thigh's length is the calf's offset, the calf's the ankle's.
bool Leg::setup() {
  const bool ok = MechPart::setup();
  if (!ok) return ok;
  Mech& m = *mech;
  knee_back = m.knee_back;
  if (g_calls.tree_update) g_calls.tree_update(m.node);
  const Vec3 f = *foot->translation;
  const Vec3 zero{0, 0, 0};  // 0x0063d870
  zmth::MatrixStack& st = zmth::matrix_stack();
  zmth::Mat34 buf;
  zmth::push(&buf);
  zmth::load(*reinterpret_cast<const zmth::Mat34*>(calf->rotation));
  // The calf's origin, then taken through the thigh (0x005227a0): unused.
  zmth::load(*reinterpret_cast<const zmth::Mat34*>(rotation));
  const Vec3 hip = st.is_identity() ? zero : through(st.matrix().m, zero);
  zmth::Vec3 a, b;
  const zmth::Vec3 fv = v3(f);
  zmth::inverse_transform_points_to(&fv, &a, 1);
  zmth::load(*reinterpret_cast<const zmth::Mat34*>(calf->rotation));
  zmth::inverse_transform_points_to(&a, &b, 1);
  zmth::pop();
  const Vec3& k = *calf->translation;
  thigh_len = std::sqrt(k.y * k.y + k.z * k.z);
  const float c = std::sqrt(b.y * b.y + b.z * b.z);
  calf_len = c;
  rest_angle = static_cast<float>(std::atan2(double{-(f.y - hip.y)}, double{f.x - hip.x}));
  ankle = {b.x, b.y, b.z};
  hip_x = f.x;
  rest_x = hip.x;
  ik_k = -(thigh_len * thigh_len + c * c);
  const float tc = c * thigh_len;
  ik_m = 1.0f / (tc + tc);
  calf->setup();
  foot->setup();
  return ok;
}

// Mech3.exe 1.2 @ 0x00462860: the splash effect is made whether or not the
// foot has a node.
bool Foot::setup() {
  const bool ok = MechPart::setup();
  effect = g_calls.foot_effect ? g_calls.foot_effect() : nullptr;
  return ok;
}

// Mech3.exe 1.2 @ 0x00464290: the definition's shake (its scales, frequency
// and damping) and turn limits; the half width and depth from the box of the
// torso's `torso_polys` node (the torso's own without one); then the cockpit
// off the torso (0x00464580(0)), whether or not the base set-up worked.
bool Torso::setup() {
  const bool ok = MechPart::setup();
  if (ok) {
    const MechDfn* d = dfn(*mech);
    for (int k = 0; k < 3; ++k) jolt_scale[k] = d->get<float>(0x160 + 4 * k);
    freq = d->get<float>(0x16c);
    damp = d->get<float>(0x170);
    max_yaw = d->get<float>(0x174);
    max_pitch = d->get<float>(0x178);
    min_pitch = d->get<float>(0x17c);
    // (A missing +4 is found again from the node here too; part_construct
    // always sets it.)
    if (g_calls.tree_update) g_calls.tree_update(mech->node);
    Handle polys = g_calls.find_in_tree ? g_calls.find_in_tree(node, "torso_polys")
                                        : zcls::node_find_in_tree(static_cast<zcls::Node*>(node), "torso_polys");
    if (!polys) polys = node;
    const float* box = collide_calls().hit_box ? collide_calls().hit_box(polys)
                                               : zcls::node_box(static_cast<zcls::Node*>(polys));
    if (box) {
      half_width = (box[3] - box[0]) * 0.5f;  // 0x005c2750
      half_depth = (box[5] - box[2]) * 0.5f;
    }
  }
  set_cockpit(false);
  return ok;
}

// Mech3.exe 1.2 @ 0x00466e80
bool Hand::setup() {
  const bool ok = MechPart::setup();
  if (ok) rest = *translation;
  return ok;
}

// Mech3.exe 1.2 @ 0x004663e0: the aim and recoil settings from the
// definition (def +0x180..+0x1a4); a hand that cannot be set up is dropped.
bool Arm::setup() {
  const bool ok = MechPart::setup();
  if (ok) {
    const MechDfn* d = dfn(*mech);
    recoil_pitch = d->get<float>(0x180);
    recoil_push = d->get<float>(0x184);
    recoil_twist = d->get<float>(0x188);
    aim_rate = d->get<float>(0x18c);
    min_pitch = d->get<float>(0x190);
    max_pitch = d->get<float>(0x194);
    min_yaw = d->get<float>(0x198);
    max_yaw = d->get<float>(0x19c);
    hand_share = d->get<float>(0x1a0);
    hand_aims = d->get<uint8_t>(0x1a4) != 0;
  }
  recoil = 0.0f;
  x2c = 1.0f;
  rest = *translation;
  if (hand) {
    if (hand->setup()) {
      const Vec3& t = *hand->translation;
      length = std::sqrt((t.x * t.x + t.y * t.y) + t.z * t.z);
      return ok;
    }
    if (damage_calls().free_part) damage_calls().free_part(hand);
    else delete hand;
    hand = nullptr;
  }
  return ok;
}

// Mech3.exe 1.2 @ 0x004623a0: the crouch's offset toward its target at 2/s,
// snapped within 0.05, kept within [-depth, 0].
void Hip::update(float dt) {
  const float t = crouch_target;
  const float y = offset.y - (t - offset.y) * dt * kCrouchRate;
  offset.y = y;
  if (std::fabs(t - y) < kSnap) offset.y = t;
  const float depth = -mech->crouch_depth;
  if (offset.y > 0.0f) offset.y = 0.0f;
  else if (depth > offset.y) offset.y = depth;
  node_changed();
}

// Mech3.exe 1.2 @ 0x004621f0: the crouch's target (only y is used), kept
// within [-crouch depth, 0]; the update eases toward it.
void Hip::set_crouch(const Vec3* t) {
  target_x = t->x, crouch_target = t->y, target_z = t->z;
  const float depth = -mech->crouch_depth;
  if (crouch_target > 0.0f) crouch_target = 0.0f;
  else if (depth > crouch_target) crouch_target = depth;
}

// Mech3.exe 1.2 @ 0x004622a0 (a ceiling hit, mech_collide): the hip drops by
// amount x the crouch depth at once (amount within 0..1), within [-depth, 0];
// the duck sound with sounds on.
void Hip::duck(float amount) {
  Mech& m = *mech;
  const float a = amount > 1.0f ? 1.0f : !(amount >= 0.0f) ? 0.0f : amount;
  offset.y = offset.y - a * m.crouch_depth;
  const float depth = -m.crouch_depth;
  if (offset.y > 0.0f) offset.y = 0.0f;
  else if (depth > offset.y) offset.y = depth;
  if (m.detail & 1)
    m.sounds[kDuckSlot] = move_calls().pilot_sound ? move_calls().pilot_sound(m, kDuckSound, 1.0f, -1.0f) : nullptr;
}

// Mech3.exe 1.2 @ 0x00463ee0
void Leg::update(float dt) { foot->update(dt); }

// Mech3.exe 1.2 @ 0x00462de0. With the foot spin and full detail (bits 2,
// 4): a foot on the ground tilts to the ground's normal, seen in the mech's
// frame and taken out of the hip's turn; in the air it keeps the pose's.
void Foot::tilt(const GroundHit& c) {
  const uint32_t d = mech->detail;
  if (!(d & 4) || !(d & 0x10)) return;
  normal = c.normal;
  float q[4];
  if (ground_state != 0) {
    zmth::Vec3 n{c.normal.x, c.normal.y, c.normal.z};
    zmth::Mat34 buf;
    zmth::push(&buf);
    zmth::load_identity();
    to_world(mech->node, 2);
    zmth::rotate_points_transposed(&n, 1);
    zmth::pop();
    zmth::normalize(n);
    static const float up[3] = {0, 1, 0};  // 0x005eda70
    const float nv[3] = {n.x, n.y, n.z};
    float b[4];
    zmth::quat_between(up, nv, b);
    zmth::Quat r;
    zmth::quat_mul(quat(mech->hip->inv), quat(b), r);
    store(q, r);
  } else {
    for (int i = 0; i < 4; ++i) q[i] = pending_q[i];
  }
  // [0x00605cbc] would ease the foot toward it by half the frame instead;
  // nothing sets it.
  for (int i = 0; i < 4; ++i) rot[i] = q[i];
}

// Mech3.exe 1.2 @ 0x00462a10 (Foot::GroundUpdate). The ground under the foot
// from its ski's parabola, its water, its tilt; the clearance sets the
// ground state (0 air, 1 touching, 2, 3 planted). A touch picks the soil and
// hands the node underfoot a 1000 (stepping on things). Two switches
// ([0x00605cb4]: the world's own ground under the player's feet within 1 m;
// [0x00605cb8]: a second height from 0x00475d10) are never set.
float Foot::ground_update() {
  if (!node) return 0.0f;
  Mech& m = *mech;
  Skis& s = *m.skis;
  const int side = id != kLeftFoot;
  Vec3 w{0, 0, 0};
  world_pos(node, &w);
  GroundHit* contact = nullptr;
  const float ground = s.height_at(side, translation->z, &contact);
  const float above = w.y - ground;
  water_surface = 0.0f;
  in_water = s.water_at(side, translation->z, &water_surface);
  tilt(*contact);
  const float h = contact->normal.y * above;
  const float clr = h - sole;
  if (shadow) {
    // The shadow node (always 0: the constructor clears it) under the foot.
    float lift = kShadowLift + h;
    if (clr < 0.0f) lift = lift + clr;
    float* sm = pose_calls().node_matrix ? pose_calls().node_matrix(shadow) : nullptr;
    if (sm) {
      zmth::Quat r, c2, n;
      zmth::quat_mul(quat(m.hip->rot), quat(rot), r);
      zmth::quat_conjugate(r, c2);
      zmth::quat_normalize(c2, n);
      float nq[4];
      store(nq, n);
      zmth::quat_matrix(nq, sm);
      sm[10] = -lift;
      sm[9] = 0.0f;
      sm[11] = 0.0f;
      if (pose_calls().node_set) pose_calls().node_set(shadow, sm);
    }
  }
  const float prev = clearance;
  clearance = clr;
  x88 = false;
  last_clearance = prev;
  if (ground_state == 0) {
    if (prev >= clr && !(clr >= kTouch)) ground_state = 1;
  } else if (ground_state == 3 && clr > kLift) {
    ground_state = 0;
  }
  if (in_water) {
    water_depth = water_surface - w.y;
    if (water_surface - w.y < 0.0f) in_water = false;
  } else {
    if (effect && playing(effect)) stop_fx(effect);
    water_depth = 0.0f;
  }
  if (shadow && damage_calls().node_set_active) damage_calls().node_set_active(shadow, !in_water);
  if (ground_state == 1) {
    if (!contact) {
      soil = 0;
    } else {
      soil = contact->material && skis_calls().soil ? skis_calls().soil(contact->material) : 0;
      if (contact->node && g_calls.step_on) g_calls.step_on(w, *contact, kStepForce);
    }
  }
  return clearance;
}

// Mech3.exe 1.2 @ 0x00462fa0. A footprint's angles: facing along the mech's
// flat forward, laid on the ground's normal; none on a vertical face.
static bool footprint(const Vec3& fwd, const Vec3& n, float out[3]) {
  if (n.y == 1.0f) {
    out[0] = 0.0f;
    out[2] = 0.0f;
    out[1] = static_cast<float>(std::atan2(double{-fwd.x}, double{-fwd.z}));
    return true;
  }
  if (!(n.y < 0.0f || n.y > 0.0f)) return false;
  zmth::Vec3 f{fwd.x, 0.0f, fwd.z};
  f.y = (n.z * fwd.z + fwd.x * n.x) / -n.y;
  zmth::normalize(f);
  const Vec3 r{f.y * n.z - f.z * n.y, f.z * n.x - f.x * n.z, f.x * n.y - f.y * n.x};
  zmth::Mat34 m{{r.x, r.y, r.z, n.x, n.y, n.z, -f.x, -f.y, -f.z, 0, 0, 0}};
  zmth::matrix_to_euler(m, out);
  return true;
}

// Mech3.exe 1.2 @ 0x00463110. The turn-in-place counter-spin (set this frame
// by the turn, else decaying by exp(-3 dt)); the rotation onto the node; then
// the step: on the touch (1 -> 2) the player's force feedback, on the plant
// (2 -> 3) a splash or a footprint and the step sound. Far mechs only stop
// the splash.
void Foot::update(float dt) {
  if (!node) return;
  Mech& m = *mech;
  const uint32_t d = m.detail;
  if (!(d & 0x10)) {
    if (effect) stop_fx(effect);
    return;
  }
  if (pivot) {
    pivot = false;
    spin = pivot_rate * dt + spin;
  } else if (!(spin < kSpinMin) || !(spin > -kSpinMin)) {
    spin = fast_exp(dt * kSpinDecay) * spin;
  }
  if (m.airborne() || !(d & 4)) spin = 0.0f;
  const float sp = spin;
  float q[4];
  if (!(sp < kSpinMin) || !(sp > -kSpinMin)) {
    zmth::Quat y, r;
    zmth::quat_yaw(sp, y);
    zmth::quat_mul(quat(rot), y, r);
    store(q, r);
  } else {
    for (int i = 0; i < 4; ++i) q[i] = rot[i];
  }
  zmth::quat_matrix(q, rotation);
  node_changed();  // vtable +0x14, 0x004635d0
  Vec3 at = position(m);
  const World& wd = world();
  const int side = id != kLeftFoot;
  if (ground_state == 1) {
    ground_state = 2;
    if (&m == wd.mission_player && wd.force_feedback && g_calls.footstep_ff)
      g_calls.footstep_ff(std::fabs(m.vec_3c.z) / m.max_speed);
    return;
  }
  if (ground_state == 2) {
    if (d & 8) {
      world_pos(node, &at);
      at.y = at.y - sole;
      if (in_water) {
        const GroundHit& c = m.skis->contact[side ? 4 : 1];
        const float sy = m.skis->surface[side ? 4 : 1];
        soil = 1;  // water
        if (effect) {
          if (g_calls.effect_place) g_calls.effect_place(effect, c.x, sy, c.z);
          if (g_calls.effect_start) g_calls.effect_start(effect, m.controls->throttle);
        }
      } else if (!m.skis->slid) {
        float ang[3];
        if (footprint(m.forward, normal, ang)) {
          const float y = at.y - kPrintLift;
          Handle print = m.pilot->soils ? m.pilot->soils->footprint[soil] : nullptr;
          if (print && damage_calls().spawn_effect) {
            const float args[9] = {at.x, y, at.z, ang[0], ang[1], ang[2], 0, 0, 0};
            damage_calls().spawn_effect(print, nullptr, args);
          }
        }
        if (effect && playing(effect)) stop_fx(effect);
      }
    }
    if (d & 1) {
      const float vol = std::fabs(m.controls->throttle) * (1.0f - kStepVolume) + kStepVolume;
      Handle sound = m.pilot->soils ? m.pilot->soils->step_sound[soil] : nullptr;
      if (g_calls.play_at) g_calls.play_at(sound, &at, vol);
    }
    ground_state = 3;
    return;
  }
  if (effect && playing(effect)) {
    const GroundHit& c = m.skis->contact[side ? 4 : 1];
    const float sy = m.skis->surface[side ? 4 : 1];
    if (g_calls.effect_place) g_calls.effect_place(effect, c.x, sy, c.z);
  }
}

// Mech3.exe 1.2 @ 0x00465bc0: the pitch and yaw of a direction (asin of y,
// atan2 of x and -z), clamped to the torso's limits; false when clamped.
bool Torso::aim_angles(const Vec3& dir, float* p, float* y) const {
  *p = crt_asin(dir.y);
  *y = static_cast<float>(std::atan2(double{dir.x}, double{-dir.z}));
  bool pc = true, yc = true;
  if (*p > max_pitch) *p = max_pitch;
  else if (!(*p >= min_pitch)) *p = min_pitch;
  else pc = false;
  if (*y > max_yaw) *y = max_yaw;
  else if (-max_yaw > *y) *y = -max_yaw;
  else yc = false;
  return !pc && !yc;
}

// Mech3.exe 1.2 @ 0x00465c70: a direction turned by the mech's matrix
// (0x00460000's +0x0c), then aim_angles.
bool Torso::face_angles(const Vec3& d, float* p, float* y) const {
  const float* m = mech->transform;
  const Vec3 dir{(d.x * m[0] + d.y * m[1]) + d.z * m[2], (d.x * m[3] + d.y * m[4]) + d.z * m[5],
                 (d.x * m[6] + d.y * m[7]) + d.z * m[8]};
  return aim_angles(dir, p, y);
}

// Mech3.exe 1.2 @ 0x00465ad0: the aim direction (turned by the mech's matrix
// when aim_local) along the torso's forward.
float Torso::update_facing() {
  Vec3 dir = aim_dir;
  if (aim_local) {
    const float* m = mech->transform;
    dir = {(dir.x * m[0] + dir.y * m[1]) + dir.z * m[2], (dir.x * m[3] + dir.y * m[4]) + dir.z * m[5],
           (dir.x * m[6] + dir.y * m[7]) + dir.z * m[8]};
  }
  facing = (front.x * dir.x + front.y * dir.y) + front.z * dir.z;
  return facing;
}

// Mech3.exe 1.2 @ 0x0041dc70
bool Mech::can_face(const Vec3& dir) {
  float p, y;
  return torso->face_angles(dir, &p, &y);
}

// Mech3.exe 1.2 @ 0x0041dca0: a direction within the torso's limits as it
// is; clamped, the limits' (sin yaw cos pitch, sin pitch, -cos yaw cos pitch).
bool Mech::clamp_aim(const Vec3& dir, Vec3* out) {
  float p, y;
  if (torso->aim_angles(dir, &p, &y)) {
    *out = dir;
    return true;
  }
  out->y = static_cast<float>(std::sin(double{p}));
  const float cp = static_cast<float>(std::cos(double{p}));
  out->x = static_cast<float>(std::sin(double{y}));
  out->z = static_cast<float>(std::cos(double{y}));
  out->x = out->x * cp;
  out->z = -(cp * out->z);
  return false;
}

// Mech3.exe 1.2 @ 0x0041dba0 (the torso's 0x00465bb0: +0x98)
bool Mech::faces_within(float cosine) { return torso->facing > cosine; }

// Mech3.exe 1.2 @ 0x0041dbd0: the AI's aim in the world's frame, or off;
// then the facing again.
void Mech::set_torso_aim(const Vec3* dir) {
  Torso& t = *torso;
  if (dir) {
    t.aim = 1;
    t.aim_dir = *dir;
    t.aim_local = 0;
  } else {
    t.aim = 0;
  }
  t.update_facing();
}

// Mech3.exe 1.2 @ 0x0041d400: airborne (vtable +0x144: +0x584) its world
// velocity; on the ground its forward (+0x18) at the throttle's share of
// its top speed (the controls' vt+0x1c).
void Mech::lead_velocity(Vec3* out) const {
  if (airborne()) {
    *out = velocity;
    return;
  }
  const float k = max_speed * controls->throttle;
  out->x = k * x18.x;
  out->y = x18.y * k;
  out->z = x18.z * k;
}

// Mech3.exe 1.2 @ 0x00465d10. The AI's aim direction (in the mech's frame
// when aim_local), its cosine to the torso's forward, its angles; clamped,
// the direction becomes the clamped angles' (sin yaw, sin pitch, -cos yaw).
bool Torso::aim_view(Vec3* out, float* p, float* y) {
  Vec3 dir = aim_dir;
  if (aim_local) {
    const float* m = mech->transform;
    dir = {(dir.x * m[0] + dir.y * m[1]) + dir.z * m[2], (dir.x * m[3] + dir.y * m[4]) + dir.z * m[5],
           (dir.x * m[6] + dir.y * m[7]) + dir.z * m[8]};
  }
  facing = (front.x * dir.x + front.y * dir.y) + front.z * dir.z;
  if (aim_angles(dir, p, y)) {
    *out = dir;
    return false;
  }
  out->y = static_cast<float>(std::sin(double{*p}));
  out->x = static_cast<float>(std::sin(double{*y}));
  out->z = static_cast<float>(std::cos(double{*y}));
  out->z = -out->z;
  return true;
}

// Mech3.exe 1.2 @ 0x00464ce0: the player's rate mode. The inputs turn yaw
// and pitch at dt per second (x 0.3 with the HUD's bit 4 clear), clamped to
// the limits; the pitch lock levels at 1.75/s and drops the pitch input.
bool Torso::steer(float dt, float* y, float* p, const float* yaw_in, float* p_in) {
  if (pitch_lock) {
    const float n = kLevel * dt * -*p + *p;
    *p = n;
    if (std::fabs(-n) < kSnap) *p = 0.0f;
    *p_in = 0.0f;
  }
  const float f = slow_turn(*mech) ? kSlow : 1.0f;
  *p = dt * *p_in * f + *p;
  *y = dt * *yaw_in * f + *y;
  bool yc = true, pc = true;
  if (*y > max_yaw) *y = max_yaw;
  else if (!(*y >= -max_yaw)) *y = -max_yaw;
  else yc = false;
  if (*p > max_pitch) *p = max_pitch;
  else if (!(*p >= min_pitch)) *p = min_pitch;
  else pc = false;
  return yc || pc;
}

// Mech3.exe 1.2 @ 0x00465940: the torso's turn, the arms' recoil twist, the
// jolt's spin and the shake spring's position, as one rotation.
void Torso::shake_q(float out[4]) const {
  zmth::Quat k, a, b, e, c, n;
  zmth::quat_yaw(kick_twist, k);
  zmth::quat_mul(quat(turn_q), k, a);
  zmth::quat_mul(a, quat(jolt_q), b);
  zmth::quat_exp(v3(jolt_pos), e);
  zmth::quat_mul(b, e, c);
  zmth::quat_normalize(c, n);
  store(out, n);
}

// Mech3.exe 1.2 @ 0x004659c0: the pose's rotation, then the turn and shake.
void Torso::node_changed() {
  float r[4];
  shake_q(r);
  zmth::Quat o;
  zmth::quat_mul(quat(rot), quat(r), o);
  float oq[4];
  store(oq, o);
  zmth::quat_matrix(oq, rotation);
  MechPart::node_changed();
}

// Mech3.exe 1.2 @ 0x00464e80. Mode 0, the AI: toward the aim by exp(-2 dt)
// (not while the pitch is locked). Mode 1, the player: the pitch lock, the
// legs turning under the torso (CMD_CENTER_LEGS), the rate or absolute
// inputs and the free look, the servo sound. Then for every mode the arms'
// recoil twist and the jolt decay, and the shake spring in 1/15 s substeps
// (already frame-rate independent).
void Torso::update(float dt) {
  float* aimed = view_aim ? view_q : turn_q;
  if (mode == 0) {
    if (aim) {
      Vec3 dir;
      float p, y;
      aim_view(&dir, &p, &y);
      if (!pitch_lock) {
        float k = fast_exp(kAimDecay * dt);
        if (k < kAimFloor) k = 0.0f;
        const float u = 1.0f - k;
        pitch = p * u + k * pitch;
        yaw = k * yaw - y * u;
      }
      yaw_pitch(yaw, pitch, aimed);
      zmth::Vec3 f;
      zmth::quat_rotate(quat(aimed), {0, 0, -1}, f);  // 0x005e4f40
      front = {f.x, f.y, f.z};
      zmth::Vec3 flat{f.x, 0.0f, f.z};
      zmth::normalize(flat);
      flat_front = {flat.x, flat.y, flat.z};
      facing = (front.x * dir.x + front.y * dir.y) + front.z * dir.z;
    }
  } else if (mode == 1) {
    Mech& m = *mech;
    bool clamped = false;
    const Sound servo = m.servo();
    facing = 1.0f;
    if (pitch_lock) {
      const float n = -pitch * kLevel * dt + pitch;
      pitch = n;
      if (std::fabs(-n) < kSnap) pitch = 0.0f;
      pitch_in = 0.0f;
    }
    bool slow = slow_turn(m);
    const bool input = twist_in != 0.0f || pitch_in != 0.0f || look_yaw != 0.0 || look_pitch != 0.0;
    if (input) centring = 0;
    if (centring == 2) {
      MechControls& c = *m.controls;
      if (!(c.turn < 0.0f || c.turn > 0.0f) && (yaw < 0.0f || yaw > 0.0f)) {
        slow = false;
        const float t = kLevel * yaw;
        const float a = std::fabs(std::fabs(c.throttle));
        const float r = (1.0f - a * 0.8f) * m.turn_rate;  // 0x00418fa0
        c.turn = t / r;
        if (update_calls().turn) update_calls().turn(m, dt);
        else m.turn(dt);  // vtable +0x170
        c.turn = 0.0f;
      } else {
        centring = 0;
      }
    }
    float rate = 0.0f;
    bool approach_mode = absolute;
    if (!absolute) {
      if (centring != 0) {
        rate = kCentre;
        approach_mode = true;
      } else {
        const MechDfn* d = mech_def(m);
        max_yaw = d->get<float>(0x174);
        max_pitch = d->get<float>(0x178);
        min_pitch = d->get<float>(0x17c);
        clamped = steer(dt, &yaw, &pitch, &twist_in, &pitch_in);
        if (g_calls.look_limits) g_calls.look_limits(m, &max_yaw, &max_pitch, &min_pitch);
        steer(dt, &look_yaw_angle, &look_pitch_angle, &look_yaw, &look_pitch);
      }
    } else {
      rate = centring != 0 ? kCentre : kLevel;
    }
    if (approach_mode) {
      if (rate == kCentre && slow) rate = kSlow;
      const MechDfn* d = mech_def(m);
      max_yaw = d->get<float>(0x174);
      max_pitch = d->get<float>(0x178);
      min_pitch = d->get<float>(0x17c);
      approach(&yaw, max_yaw * twist_in, rate, dt);
      approach(&pitch, pitch_in >= 0.0f ? max_pitch * pitch_in : -(min_pitch * pitch_in), rate, dt);
      if (g_calls.look_limits) g_calls.look_limits(m, &max_yaw, &max_pitch, &min_pitch);
      approach(&look_yaw_angle, look_yaw * max_yaw, rate, dt);
      approach(&look_pitch_angle, (look_pitch >= 0.0f ? max_pitch : -min_pitch) * look_pitch, rate, dt);
    }
    yaw_pitch(yaw, pitch, turn_q);
    yaw_pitch(look_yaw_angle + yaw, look_pitch_angle + pitch, view_q);
    if (!servo) {
      if (!view_aim && input && !clamped)
        m.servo() = g_calls.sound ? g_calls.sound(m, kServoSound, kServoVolume) : nullptr;
    } else if ((view_aim || !input || clamped) && m.servo()) {
      if (heat_calls().stop_sound) heat_calls().stop_sound(m.servo());
      m.servo() = nullptr;
    }
  }
  // The arms' recoil twist and its spring, the jolt's spin.
  kick = (1.0f - dt * 3.0f) * kick;
  kick_twist = (kick_twist - kick) * (1.0f - dt * 5.0f) + kick;
  const float k = 1.0f - dt * 0.9f;
  jolt_spin = {k * jolt_spin.x, k * jolt_spin.y, k * jolt_spin.z};
  jolt_turn = {jolt_turn.x - jolt_spin.x, jolt_turn.y - jolt_spin.y, jolt_turn.z - jolt_spin.z};
  const float k2 = 1.0f - dt * 2.5f;
  jolt_turn = {k2 * jolt_turn.x, k2 * jolt_turn.y, k2 * jolt_turn.z};
  jolt_turn = {jolt_turn.x + jolt_spin.x, jolt_turn.y + jolt_spin.y, jolt_turn.z + jolt_spin.z};
  zmth::Quat jq;
  zmth::quat_exp(v3(jolt_turn), jq);
  store(jolt_q, jq);
  float h = kShakeStep, left = dt;
  if (dt > 0.0f) {
    do {
      if (!(left >= kShakeStep)) h = left;
      const Vec3 dv{jolt.x * h, jolt.y * h, jolt.z * h};
      jolt_pos = {dv.x + jolt_pos.x, dv.y + jolt_pos.y, dv.z + jolt_pos.z};
      const float a = -(freq * freq) * h;
      const Vec3 acc{a * jolt_pos.x, a * jolt_pos.y, a * jolt_pos.z};
      jolt = {acc.x + jolt.x, acc.y + jolt.y, acc.z + jolt.z};
      const float g = 1.0f - h * damp;
      jolt = {g * jolt.x, g * jolt.y, g * jolt.z};
      left = left - kShakeStep;
    } while (left > 0.0f);
  }
  zmth::Vec3 f;
  zmth::quat_rotate(quat(turn_q), {0, 0, -1}, f);  // 0x005e4f58
  front = {f.x, f.y, f.z};
  zmth::Vec3 flat{f.x, 0.0f, f.z};
  zmth::normalize(flat);
  flat_front = {flat.x, flat.y, flat.z};
  node_changed();  // vtable +0x14, 0x004659c0
  MechPart::update(dt);
}

// Mech3.exe 1.2 @ 0x00466610 (vtable +0x24): a weapon's kick. The recoil
// grows (to 1) and twists the torso by the def's recoil twist, left arm one
// way and right the other, within that twist either side.
void Arm::kick(float amount) {
  recoil = amount + recoil;
  if (recoil > 1.0f) recoil = 1.0f;
  else if (!(recoil >= 0.0f)) recoil = 0.0f;
  Torso& t = *mech->torso;
  const float k0 = t.kick;
  const float d = amount * recoil_twist;
  float n = id == kLeftArm ? d + k0 : k0 - d;
  if (n > recoil_twist) n = recoil_twist;
  else if (!(n >= -recoil_twist)) n = -recoil_twist;
  t.kick = n;
}

// Mech3.exe 1.2 @ 0x004666f0: the recoil's pose. With a hand the arm pitches
// up by asin(recoil_pitch x recoil) and the hand, turned back, slides back
// by recoil_push x recoil; without, the arm itself slides back.
void Arm::recoil_pose() {
  const zmth::Vec3 v{0.0f, 0.0f, recoil_push * recoil};
  if (hand) {
    const float a = crt_asin(-(recoil_pitch * recoil));
    zmth::Quat pq, q, c, q2;
    zmth::quat_pitch(a, pq);
    zmth::quat_mul(quat(aim_q), pq, q);
    float qa[4];
    store(qa, q);
    set_pose(nullptr, qa);
    zmth::quat_conjugate(pq, c);
    zmth::quat_mul(c, quat(hand->rot), q2);
    zmth::Vec3 r;
    zmth::quat_rotate(q2, v, r);
    const Vec3 p{r.x + hand->rest.x, r.y + hand->rest.y, r.z + hand->rest.z};
    store(qa, q2);
    hand->set_pose(&p, qa);
    return;
  }
  zmth::Vec3 r;
  zmth::quat_rotate(quat(aim_q), v, r);
  const Vec3 p{r.x + rest.x, r.y + rest.y, r.z + rest.z};
  set_pose(&p, nullptr);
}

// Mech3.exe 1.2 @ 0x00466840. Toward the weapons' target by exp(-rate dt):
// its direction from the arm in the torso's frame, as pitch and yaw, within
// the def's limits (the right arm's yaw mirrored); without a target back to
// rest by exp(-rate dt / 2). The hand takes all of the aim (hand_aims) or the
// pitch less the arm's share; the arm the yaw and that share.
void Arm::aim(float dt) {
  if (destroyed) return;
  Mech& m = *mech;
  Vec3 target;
  if (g_calls.aim_point && g_calls.aim_point(m, &target)) {
    Vec3 a{0, 0, 0};
    world_pos(node, &a);
    float e[3] = {0, 0, 0};
    if (g_calls.node_angles) g_calls.node_angles(m.torso->node, e);
    else zcls::node_world_euler(static_cast<zcls::Node*>(m.torso->node), e);
    float tq[4];
    zmth::euler_to_quat(e[1], e[0], e[2], tq);
    const zmth::Vec3 dv{target.x - a.x, target.y - a.y, target.z - a.z};
    zmth::Quat c;
    zmth::quat_conjugate(quat(tq), c);
    zmth::Vec3 l;
    zmth::quat_rotate(c, dv, l);
    static const float zero[3] = {0, 0, 0};  // 0x0063d870
    const float lv[3] = {l.x, l.y, l.z};
    float ang[3];
    zmth::direction_angles(zero, lv, ang);
    const float k = zmth::exp_neg_dt(aim_rate * dt, dt);
    aim_pitch = (aim_pitch - ang[0]) * k + ang[0];
    aim_yaw = (aim_yaw - ang[1]) * k + ang[1];
    if (aim_pitch > max_pitch) aim_pitch = max_pitch;
    else if (!(aim_pitch >= min_pitch)) aim_pitch = min_pitch;
    if (id == kLeftArm) {
      if (aim_yaw > max_yaw) aim_yaw = max_yaw;
      else if (!(aim_yaw >= min_yaw)) aim_yaw = min_yaw;
    } else {
      const float hi = -min_yaw, lo = -max_yaw;
      if (!(hi >= aim_yaw)) aim_yaw = hi;
      else if (!(aim_yaw >= lo)) aim_yaw = lo;
    }
  } else {
    const float k = zmth::exp_neg_dt(aim_rate * dt * 0.5f, dt);
    aim_pitch = k * aim_pitch;
    aim_yaw = k * aim_yaw;
  }
  if (!hand) {
    zmth::euler_to_quat(aim_yaw, aim_pitch, 0.0f, aim_q);
    set_pose(nullptr, aim_q);
    return;
  }
  if (hand_aims) {
    set_pose(nullptr, aim_q);
    float q[4];
    zmth::euler_to_quat(aim_yaw, aim_pitch, 0.0f, q);
    for (int i = 0; i < 4; ++i) hand->rot[i] = q[i];
    hand->set_pose(nullptr, q);
    return;
  }
  const float y = aim_yaw;
  const float share = std::fabs(y) * hand_share;
  zmth::euler_to_quat(y, share, 0.0f, aim_q);
  set_pose(nullptr, aim_q);
  zmth::Quat p;
  zmth::quat_pitch(aim_pitch - share, p);
  store(hand->rot, p);
  hand->set_pose(nullptr, hand->rot);
}

// Mech3.exe 1.2 @ 0x00466510: aim, the recoil's decay toward its hold (each
// by exp(-4 dt) and exp(-6 dt)), the hand, the water check. Full detail only.
void Arm::update(float dt) {
  if (!(mech->detail & 0x10)) return;
  aim(dt);
  if (recoil > 0.0f) {
    const float e1 = zmth::exp_neg_dt(dt * kRecoilHold, dt);
    recoil_hold = e1 * recoil_hold;
    const float e2 = zmth::exp_neg_dt(dt * kRecoilDecay, dt);
    recoil = (recoil - recoil_hold) * e2 + recoil_hold;
    recoil_pose();
    if (recoil < kRecoilMin) recoil = 0.0f;
  }
  if (hand) hand->update(dt);
  MechPart::update(dt);
}

// Mech3.exe 1.2 @ 0x0041d1d0
float Mech::throttle_speed() const { return controls ? controls->throttle * max_speed : 0.0f; }

}  // namespace mech3
