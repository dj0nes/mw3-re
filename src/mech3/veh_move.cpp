#include "mech3/veh_move.h"

#include <cmath>
#include <cstring>

#include "gamez/zclass/cls_di.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
#include "mech3/veh_basic.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
VehBasicHooks& hk() { return veh_basic_hooks(); }
float sign1(float x) { return x >= 0.0f ? 1.0f : -1.0f; }  // the exe's: 1 at 0, -1 for NaN
}  // namespace

// Mech3.exe 1.2 @ 0x004a2a00: the turn's own (turns[0]), damped to 0 without one.
void VehicleControls::turn_step(const VehicleMotion& m, float dt) {
  const float t = turn;  // IControls vt+0x24
  if (t == 0.0f || std::isnan(t)) {
    x50.y = zmth::exp_neg(m.turn_damping * dt) * x50.y;
    return;
  }
  if ((t > 0.0f && !(x50.y >= 0.0f)) || (t < 0.0f && x50.y > 0.0f)) x50.y = 0.0f;
  const float r = (m.turns[0] * t) * dt + x50.y;
  x50.y = r;
  if (r > top_turn) x50.y = top_turn;
  else if (!(r >= -top_turn)) x50.y = -top_turn;
}

// Mech3.exe 1.2 @ 0x004a28c0
void vehicle_turn_to_heading(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  if (!c.x760) return;
  if (c.turn != 0.0f && !std::isnan(c.turn)) c.x760 = 0;  // IControls vt+0x24 (a NaN counts as none)
  if (!c.x760) return;
  const float dt = hk().frame_dt();
  const float step = static_cast<float>(std::cos(double{dt * c.motion.turns[1]}));
  const float dot = v.forward.z * c.heading.z + v.forward.x * c.heading.x;
  const float cross = c.heading.x * v.forward.z - v.forward.x * c.heading.z;
  if (step > dot) {  // further than a frame's turn
    const float s = cross < 0.0f || std::isnan(cross) ? -1.0f : 1.0f;
    c.turn = s;  // IControls vt+0x28
    c.x50.y = s * c.motion.turns[1];
    return;
  }
  c.x760 = 0;
  c.turn = 0.0f;
  c.angles.y = static_cast<float>(std::atan2(-double{c.heading.x}, -double{c.heading.z}));
  c.x50.y = 0.0f;
}

// Mech3.exe 1.2 @ 0x004a2890
void vehicle_up_refresh(BasicVehicle& v) {
  hk().refresh(v);
  const float* m = v.transform;
  v.controls->up = {m[3], m[4], m[5]};
}

// Mech3.exe 1.2 @ 0x004a2b10
void vehicle_speed_step(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  if (c.sliding) return vehicle_slide(v);
  const VehicleMotion& m = c.motion;
  const float dt = hk().frame_dt();
  const float t = c.throttle;  // IControls vt+0x1c, asked each time
  if (t == 0.0f || std::isnan(t)) {
    c.x70.z = zmth::exp_neg(m.rate_damping[1] * dt) * c.x70.z;
  } else {
    // "Below" tests pass unordered values (the x87's C0), "above" ones do not.
    if ((t > 0.0f && c.x70.z > 0.0f) || (t < 0.0f && !(c.x70.z >= 0.0f)))
      c.x70.z = zmth::exp_neg(m.rate_damping[1] * dt) * c.x70.z;
    c.x70.z = c.x70.z - (t * m.rates[0]) * dt;
    const float cap = std::fabs(t) * c.top_speed;
    if (c.x70.z > cap) c.x70.z = cap;
    else if (!(c.x70.z >= -cap)) c.x70.z = -cap;
  }
  if (c.x70.x != 0.0f && !std::isnan(c.x70.x)) c.x70.x = zmth::exp_neg(m.rate_damping[0] * dt) * c.x70.x;
  if (!(std::fabs(c.x70.x) >= 0.01f)) c.x70.x = 0.0f;
}

// Mech3.exe 1.2 @ 0x004a2dd0
void vehicle_slide(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  const float dt = hk().frame_dt();
  const float fx = v.forward.x, fz = v.forward.z;
  c.x70.x = -fz * c.velocity.x - -fx * c.velocity.z;
  c.x70.z = -fz * c.velocity.z + -fx * c.velocity.x;
  const float top = c.top_speed;
  const float z = c.x70.z - (c.throttle * c.motion.rates[0]) * dt;
  c.x70.z = z;
  if (z > top) c.x70.z = top;
  else if (!(z >= -top)) c.x70.z = -top;
  float s = vehicle_side_force(v) * dt + c.x70.x;
  if (c.x70.x != 0.0f && !std::isnan(c.x70.x) && sign1(c.x70.x) != sign1(s)) {
    s = 0.0f;
    c.sliding = 0;
  }
  c.x70.x = s;
}

// Mech3.exe 1.2 @ 0x004a2c90
float vehicle_side_force(BasicVehicle& v) {
  const float dt = hk().frame_dt();
  if (!(double{dt} >= 1e-07)) return 0.0f;  // a NaN too
  VehicleControls& c = *v.controls;
  const float f = v.transform[1] * -60.0f +
                  -c.x70.z * (v.forward.x * c.x314.z - v.forward.z * c.x314.x) * hk().frame_inv_dt();
  if (c.x70.x == 0.0f || std::isnan(c.x70.x)) {  // the exe's equal test passes a NaN
    if (c.motion.friction[0] < std::fabs(f)) {
      c.sliding = 1;
      return f - sign1(f) * c.motion.friction[0];
    }
    return 0.0f;
  }
  return f - sign1(c.x70.x) * c.motion.friction[1];
}

// ---------------------------------------------------------------------------
// The suspension (2c2)

// Mech3.exe 1.2 @ 0x004a2820
void vehicle_support_step(float* y, float* vs, float ground, int32_t* air, float g, float landing) {
  const float dt = hk().frame_dt();
  const float v = *vs - g * dt;
  *vs = v;
  const float next = v * dt + *y;
  if (ground < next) {
    *air = 1;
    *y = next;
    return;
  }
  if (*air) {
    *vs = landing;
    *y = ground;
    *air = 0;
    return;
  }
  // FIX (B): the ground's step over this frame launched the vehicle harder the
  // shorter the frame; on frames under 1/30 s it counts as over a 1/30 s frame
  // (vehicles.md, High frame rates).
  const float inv = hk().frame_inv_dt();
  *vs = (ground - *y) * (zfix::options().per_second && inv > zfix::kReferenceFps ? zfix::kReferenceFps : inv);
  *y = ground;
  *air = 0;
}

// Mech3.exe 1.2 @ 0x004a23f0. Each support's sum of downhill pushes on steep
// polygons (0x004a2770) ends in a local the exe never reads: left out.
bool vehicle_support_ground(BasicVehicle& v, float* heights, float* top, float* slopes, int32_t* counts) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  for (int i = 0; i < 100; ++i) counts[i] = 0;
  const int32_t flag = h.node_flag8(v.node);
  h.node_set_flag8(v.node, 0);
  const float* t = v.transform;
  const int32_t n = m.support_count;
  for (int32_t i = 0; i < n; ++i) {
    const Vec3c& s = m.support[i];
    m.support[8 + i] = {((s.x * t[0] + s.y * t[3]) + s.z * t[6]) + t[9], ((s.x * t[1] + s.y * t[4]) + s.z * t[7]) + t[10],
                        ((s.x * t[2] + s.y * t[5]) + s.z * t[8]) + t[11]};
  }
  static zcls::GroundHits recs[8];  // 4 on the exe's stack (every vehicle has 4 supports)
  for (auto& r : recs) r = zcls::GroundHits{};
  h.ground_many(&m.support[8].x, n, 500.0f, recs);
  h.node_set_flag8(v.node, flag);
  bool missing = false;
  for (int32_t i = 0; i < n; ++i)
    if (recs[i].count == 0) {
      missing = true;
      break;
    }
  *top = -200.0f;
  if (missing) return false;
  for (int32_t i = 0; i < n; ++i) {
    int32_t best = 0, water = 0;
    float surface = 0;
    heights[i] = ground_pick(recs[i], m.support[8 + i].y, 4.0f, &best, 1, &water, &surface);
    const zcls::GroundHit& g = recs[i].hit[best];
    *top = *top > surface ? *top : surface;
    slopes[i] = -(c.velocity.x * g.normal[0] + c.velocity.z * g.normal[2]);
    if (i == 0 && recs[0].count > 0) {
      uint8_t rec[4];
      std::memcpy(rec, &g.zone[0], 4);
      const uint8_t* cam = h.camera_zones();
      int32_t zone = cam ? zmod::zone_common(cam, rec) : 0xff;
      if (zone == -1) zone = rec[1];
      v.x64 = rec[0];  // 0x005a80d0
      std::memcpy(v.zones, rec + 1, 3);
      h.node_set_zone(v.node, static_cast<uint8_t>(zone));
    }
    ++counts[water];
    // The exe's quicksand (3) and lava (4) supports sit 0.3 under the
    // surface; the pick's flag is only ever 0 or 1.
    if ((water == 3 || water == 4) && !(heights[i] > *top)) heights[i] = *top - 0.3f;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004a2190 (EDX `heights`; `flag` the pick's water
// switch, 0 from BASIC). The supports into the world, each raised by the
// frame's vertical motion, one query from 500 above, each point's ground
// picked within max(1 - that rise, 4); the highest water surface in *top
// (-200 without); the first point's zone the vehicle's. Unlike
// 0x004a23f0 an empty record is not refused: its pick gives 0.
void vehicle_platform_ground(BasicVehicle& v, float* heights, float* top, int32_t flag, int32_t* counts) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  for (int i = 0; i < 100; ++i) counts[i] = 0;
  const int32_t flag8 = h.node_flag8(v.node);
  h.node_set_flag8(v.node, 0);
  const float rise = c.velocity.y * h.frame_dt();
  const float* t = v.transform;
  const int32_t n = m.support_count;
  for (int32_t i = 0; i < n; ++i) {
    const Vec3c& s = m.support[i];
    m.support[8 + i] = {((s.x * t[0] + s.y * t[3]) + s.z * t[6]) + t[9], ((s.x * t[1] + s.y * t[4]) + s.z * t[7]) + t[10],
                        ((s.x * t[2] + s.y * t[5]) + s.z * t[8]) + t[11]};
    m.support[8 + i].y = rise + m.support[8 + i].y;
  }
  float step = 1.0f - rise;
  if (!(4.0f <= step)) step = 4.0f;  // the x87's compare: NaN keeps it
  static zcls::GroundHits recs[8];
  for (auto& r : recs) r = zcls::GroundHits{};
  h.ground_many(&m.support[8].x, n, 500.0f, recs);
  h.node_set_flag8(v.node, flag8);
  *top = -200.0f;
  for (int32_t i = 0; i < n; ++i) {
    int32_t best = 0, water = 0;
    float surface = 0;
    heights[i] = ground_pick(recs[i], m.support[8 + i].y, step, &best, flag, &water, &surface);
    *top = *top > surface ? *top : surface;
    if (i == 0 && recs[0].count > 0) {
      const zcls::GroundHit& g = recs[0].hit[best];
      uint8_t rec[4];
      std::memcpy(rec, &g.zone[0], 4);
      const uint8_t* cam = h.camera_zones();
      int32_t zone = cam ? zmod::zone_common(cam, rec) : 0xff;
      if (zone == -1) zone = rec[1];
      v.x64 = rec[0];  // 0x005a80d0
      std::memcpy(v.zones, rec + 1, 3);
      h.node_set_zone(v.node, static_cast<uint8_t>(zone));
    }
    ++counts[water];
  }
}

// Mech3.exe 1.2 @ 0x004a1f00: BASIC's ground: the turn rate's top from the
// motion (turns[1]), the highest support's ground plus mode_alt under the
// vehicle, pitch and roll 0.
void vehicle_basic_ground(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  float heights[16] = {}, top = 0;
  int32_t counts[100];
  vehicle_platform_ground(v, heights, &top, 0, counts);
  c.top_turn = m.turns[1];
  float high = heights[0];
  for (int32_t i = 1; i < m.support_count && i < 16; ++i)
    if (high < heights[i]) high = heights[i];
  c.angles.x = 0, c.angles.z = 0;
  const float* p = h.entity_position(v);
  const float to[3] = {p[0], m.mode_alt + high, p[2]};
  h.set_position(v, to);  // vt+0x18
}

// Mech3.exe 1.2 @ 0x004a20d0: BASIC's steering. A heading asked for
// (+0x760) becomes the yaw at once, the turn dropped; then the turn rate's
// step and the yaw turned by it, wrapped once past 2 pi.
void vehicle_basic_steer(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  if (c.x760) {
    c.angles.y = static_cast<float>(std::atan2(-double{c.heading.z}, -double{c.heading.x}));
    c.turn = 0;  // IControls vt+0x28
    c.x50.y = 0;
    c.x760 = 0;
  }
  c.turn_step(c.motion, hk().frame_dt());  // vt+0x14
  const float yaw = c.x50.y * hk().frame_dt() + c.angles.y;
  c.angles.y = yaw;
  if (yaw < -6.2831855f) c.angles.y = yaw - -6.2831855f;
  else if (6.2831855f < yaw) c.angles.y = yaw - 6.2831855f;
}

// Mech3.exe 1.2 @ 0x004a2000: BASIC under the overhead camera (mode 3):
// north up (yaw 0), the turn input on the sideways speed, which stops dead
// when the input would carry it the way it already goes.
void vehicle_basic_pan(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  c.angles.y = 0;
  const float t = c.turn;  // IControls vt+0x24
  if (t == 0.0f) {
    c.x70.x = 0;
    return;
  }
  if ((t > 0.0f && c.x70.x > 0.0f) || (t < 0.0f && c.x70.x < 0.0f)) c.x70.x = 0;
  c.x70.x = c.x70.x - t * c.motion.rates[0] * hk().frame_dt();
}

void VehBasicHooks::basic_step(BasicVehicle& v) { vehicle_basic_step(v); }
void VehBasicHooks::basic_ground(BasicVehicle& v) { vehicle_basic_ground(v); }

// Mech3.exe 1.2 @ 0x004a1d90: BASIC's step (the stealth object's movement).
void vehicle_basic_step(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  const bool overhead = h.view_camera_id() == 3;
  float kept = 0;
  if (overhead) {
    vehicle_basic_pan(v);
    kept = c.x70.x;
  } else {
    vehicle_basic_steer(v);
  }
  euler_rotation(&c.angles.x, v.transform);  // 0x004ab990
  vehicle_up_refresh(v);                    // 0x004a2890
  c.top_speed = c.motion.rates[1];
  vehicle_speed_step(v);  // 0x004a2b10
  if (h.view_camera_id() == 3) c.x70.x = kept;
  const float fz = v.forward.z, nfx = -v.forward.x;
  c.velocity.y = c.x70.y;
  c.velocity.x = -fz * c.x70.x + nfx * c.x70.z;
  c.velocity.z = -fz * c.x70.z - nfx * c.x70.x;
  const float* p = h.entity_position(v);
  const float dt = h.frame_dt();
  const float to[3] = {dt * c.velocity.x + p[0], p[1], dt * c.velocity.z + p[2]};
  h.set_position(v, to);  // vt+0x18
  vehicle_basic_ground(v);
  h.node_set_rotation(v.node, c.angles.x, c.angles.y, c.angles.z);  // 0x004fbd80
  h.refresh(v);                                                     // 0x005a85c0
  for (int i = 0; i < 12; ++i) c.last_matrix[i] = v.transform[i];
  c.x314 = {v.forward.x, v.forward.y, v.forward.z};
}

// Mech3.exe 1.2 @ 0x004abf70
void height_on_plane(float pos[3], const float at[3], const float n[3]) {
  if (n[1] == 0.0f || std::isnan(n[1])) {
    pos[1] = at[1];
    return;
  }
  const float r = 1.0f / n[1];
  pos[1] = ((pos[0] - at[0]) * -(r * n[0]) + -(n[2] * r) * (pos[2] - at[2])) + at[1];
}

// Mech3.exe 1.2 @ 0x004a3e40
void vehicle_forward_from_up(BasicVehicle& v) {
  const Vec3c& u = v.controls->up;
  if (u.y == 0.0f || std::isnan(u.y)) return;
  float f[3] = {v.forward.x, -((u.x * v.forward.x + v.forward.z * u.z) / u.y), v.forward.z};
  vec_normalise(f);
  v.x18 = {f[0], f[1], f[2]};
}

// Mech3.exe 1.2 @ 0x004a3ed0
void vehicle_matrix_build(BasicVehicle& v) {
  float* t = v.transform;
  const Vec3& f = v.x18;
  const Vec3c& u = v.controls->up;
  t[0] = f.y * u.z - f.z * u.y;
  t[1] = f.z * u.x - f.x * u.z;
  t[2] = f.x * u.y - f.y * u.x;
  t[3] = u.x, t[4] = u.y, t[5] = u.z;
  t[6] = -f.x, t[7] = -f.y, t[8] = -f.z;
  const float* p = hk().entity_position(v);
  t[9] = p[0], t[10] = p[1], t[11] = p[2];
}

// Mech3.exe 1.2 @ 0x004a1760
int32_t vehicle_suspension(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  float heights[8] = {}, slopes[8] = {}, top = 0;
  int32_t counts[100];
  if (!vehicle_support_ground(v, heights, &top, slopes, counts)) return 0;
  if (counts[3] > 1) {
    c.top_speed = m.rates[1] * m.quicksand_slowdown;
    c.top_turn = m.turns[1] * m.quicksand_slowdown;
  } else if (counts[4] > 1) {
    c.top_speed = m.rates[1] * m.lava_slowdown;
    c.top_turn = m.turns[1] * m.lava_slowdown;
  } else {
    c.top_speed = m.rates[1];
    c.top_turn = m.turns[1];
  }
  // The supports' vertical speeds (support_word[0..], floats) and in-air
  // flags ([8..]), worked on here and stored back.
  float vs[8];
  int32_t air[8];
  std::memcpy(vs, m.support_word, sizeof vs);
  std::memcpy(air, m.support_word + 8, sizeof air);
  const int32_t n = m.support_count;
  float old[8];
  int32_t lowest = 0;  // the exe's is unset when no height is under 5000
  float low = 5000.0f;
  for (int32_t i = 0; i < n; ++i) {
    old[i] = vs[i];
    if (low > heights[i]) low = heights[i], lowest = i;
  }
  int32_t others[3] = {};
  int32_t k = 0;
  bool all_air = true;
  for (int32_t i = 0; i < n; ++i) {
    if (i == lowest) continue;
    if (k < 3) others[k++] = i;
    vehicle_support_step(&m.support[8 + i].y, &old[i], heights[i], &air[i], c.x1c, slopes[i]);
    vs[i] = old[i];
    all_air = all_air && air[i] != 0;
  }
  c.x20 = all_air ? 1 : 0;
  int32_t best = others[0];  // the exe's is unset when every speed is past 10000
  float least = 10000.0f;
  for (int32_t o : others) {
    const float a = std::fabs(vs[o]);
    if (a < least) least = a, best = o;
  }
  int32_t ia = (best + 1) % 4;
  if (ia == lowest) ia = (lowest + 1) % 4;
  int32_t ib = (ia + 1) % 4;
  if (ib == lowest) ib = (lowest + 1) % 4;
  const float v0 = vs[best];
  c.velocity.y = v0;
  c.x70.y = v0;
  const float d1 = vs[ia] - v0, d2 = vs[ib] - v0;
  const int32_t tri[3] = {ia, ib, lowest};
  Vec3c e[3];
  for (int j = 0; j < 3; ++j) {
    const Vec3c& p = m.support[tri[j]];
    const Vec3c& q = m.support[best];
    e[j] = {p.x - q.x, p.y - q.y, p.z - q.z};
  }
  float px, pz;
  if (c.x20) {
    px = (c.angles.x - -0.5236f) * -0.7f;
    if (!(px > -0.7f)) px = -0.7f;
    pz = c.angles.z * -0.7f;
  } else {
    const float inv = 1.0f / (e[1].z * e[0].x - e[0].z * e[1].x);
    px = (d1 * e[1].x - d2 * e[0].x) * inv;
    pz = (d1 * e[1].z - d2 * e[0].z) * inv;
  }
  const float kd = zmth::exp_neg(m.a_damping * h.frame_dt());
  const float rest = 1.0f - kd;
  c.x50.x = kd * c.x50.x + px * rest;
  c.x50.z = kd * c.x50.z + pz * rest;
  for (int j = 0; j < 3; ++j) vs[tri[j]] = (e[j].x * c.x50.z - e[j].z * c.x50.x) + v0;
  float nrm[3];
  zmth::poly_normal(&m.support[8 + others[0]].x, &m.support[8 + others[2]].x, &m.support[8 + others[1]].x, nrm);
  const float* p = h.entity_position(v);
  float pos[3] = {p[0], p[1], p[2]};
  height_on_plane(pos, &m.support[8 + others[0]].x, nrm);
  h.set_position(v, pos);  // vt+0x18
  c.up = {nrm[0], nrm[1], nrm[2]};
  vehicle_forward_from_up(v);
  vehicle_matrix_build(v);
  zmth::rotate_y(nrm, nrm, -c.angles.y);
  c.angles.x = static_cast<float>(std::asin(double{nrm[2]}));
  c.angles.z = static_cast<float>(std::asin(-double{nrm[0]}));
  if (c.x20) {
    auto settle = [&](float& angle, float& rate) {
      if (angle > 0.5236f) {
        angle = 0.5236f, rate = 0.0f;
        for (int i = 0; i < 4; ++i) if (vs[i] > 0.0f) vs[i] = 0.0f;
      }
      if (!(angle >= -0.5236f)) {
        angle = -0.5236f, rate = 0.0f;
        for (int i = 0; i < 4; ++i) if (vs[i] > 0.0f) vs[i] = 0.0f;
      }
    };
    settle(c.angles.x, c.x50.x);
    settle(c.angles.z, c.x50.z);
  }
  std::memcpy(m.support_word, vs, sizeof vs);
  std::memcpy(m.support_word + 8, air, sizeof air);
  return 1;
}

// ---------------------------------------------------------------------------
// The frame (2c3)

namespace {
// The collision points (the motion's) by the vehicle's matrix into `out`.
void place_points(BasicVehicle& v, Vec3c* out) {
  VehicleControls& c = *v.controls;
  const float* t = v.transform;
  for (int32_t i = 0; i < c.point_count; ++i) {
    const Vec3c& s = c.motion.collision[i];
    out[i] = {((s.x * t[0] + s.y * t[3]) + s.z * t[6]) + t[9], ((s.x * t[1] + s.y * t[4]) + s.z * t[7]) + t[10],
              ((s.x * t[2] + s.y * t[5]) + s.z * t[8]) + t[11]};
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x004a12c0
void vehicle_track_step(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  const float dt = h.frame_dt();
  vehicle_turn_to_heading(v);
  if (c.x20) c.x50.y = zmth::exp_neg(dt) * c.x50.y;
  else c.turn_step(m, dt);  // vt+0x14
  float yaw = dt * c.x50.y + c.angles.y;
  c.angles.y = yaw;
  if (!(yaw >= -6.2831855f)) c.angles.y = yaw - -6.2831855f;
  else if (yaw > 6.2831855f) c.angles.y = yaw - 6.2831855f;
  c.angles0 = c.angles;
  euler_rotation(&c.angles.x, v.transform);
  vehicle_up_refresh(v);
  if (c.x20) {
    c.x70.x = zmth::exp_neg(dt * 0.5f) * c.x70.x;
    c.x70.z = zmth::exp_neg(dt * 0.5f) * c.x70.z;
  } else {
    vehicle_speed_step(v);
  }
  const float fx = -v.forward.x, fz = -v.forward.z;
  c.velocity.y = c.x70.y;
  c.velocity.x = fx * c.x70.z + fz * c.x70.x;
  c.velocity.z = fz * c.x70.z - fx * c.x70.x;
  const float* p = h.entity_position(v);
  float pos[3] = {p[0], p[1], p[2]};
  pos[0] = dt * c.velocity.x + pos[0];
  pos[2] = dt * c.velocity.z + pos[2];
  h.set_position(v, pos);  // vt+0x18
  c.x7c = c.velocity;
  place_points(v, c.x_ac);
  if (c.dynamics() == 0) h.collisions(v);  // vt+0x20
  if (m.mode == VehicleMotion::kBasic) {
    h.basic_ground(v);  // 0x004a1f00
    c.x20 = 0;
  } else if (m.mode == VehicleMotion::kTrack) {
    vehicle_suspension(v);
  }
  if (c.shadow) {
    if (c.x20 && c.x24) h.node_set_active(c.shadow, 0);
    else if (!c.x20 && !c.x24) h.node_set_active(c.shadow, 1);
  }
  c.x24 = c.x20;
  h.node_set_rotation(v.node, c.angles.x, c.angles.y, c.angles.z);
  h.refresh(v);
  h.node_update(v.node);
  place_points(v, c.x_ac);
  for (int32_t i = 0; i < c.point_count; ++i) c.points[i] = c.x_ac[i];
  std::memcpy(c.last_matrix, v.transform, sizeof c.last_matrix);
  c.x314 = {v.forward.x, v.forward.y, v.forward.z};
  if (m.rtracks) {
    const float r = c.x50.y * 2.25f - c.x70.z;
    h.model_scroll(h.node_model(m.rtracks), 0.0f, r * 1.72f);
    const float l = -c.x70.z - c.x50.y * 2.25f;
    h.model_scroll(h.node_model(m.ltracks), 0.0f, l * 1.72f);
  }
}

void VehBasicHooks::move(BasicVehicle& v) { vehicle_move(v); }
void VehBasicHooks::collisions(BasicVehicle& v) { vehicle_collisions(v); }
void VehBasicHooks::wall_response(BasicVehicle& v, const CollisionHit& hit) { vehicle_wall_response(v, hit); }
bool VehBasicHooks::touchable(Handle node) {  // as the mechs' (app_mech.cpp)
  const zwep::RunHooks& rh = zwep::run_hooks();
  const uintptr_t t = node && rh.handler ? rh.handler(node) : 0;
  return t > 1 && reinterpret_cast<const zwep::NodeHandlers*>(t)->slot[1].fn != nullptr;
}
float VehBasicHooks::touch(Handle node, const float from[3], const CollisionHit& h, float damage) {
  zwep::HitRecord r;
  std::memcpy(&r.normal, h.hit.normal, 12);
  std::memcpy(&r.point, h.hit.point, 12);
  r.material = h.hit.material, r.node = node;
  const zwep::Vec3f3 f{from[0], from[1], from[2]};
  return zwep::touch(&f, &r, damage);
}

// Mech3.exe 1.2 @ 0x004a1240
void vehicle_move(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  if (c.frozen) return;
  if (c.motion.mode == VehicleMotion::kBasic) hk().basic_step(v);
  else if (c.motion.mode == VehicleMotion::kTrack) vehicle_track_step(v);
  static const float kAhead[3] = {0.0f, 0.0f, -1.0f};  // 0x005e8120
  const float to[3] = {v.x18.x, v.x18.y, v.x18.z};
  zmth::quat_between(kAhead, to, v.orientation);
  v.velocity = {c.velocity.x, c.velocity.y, c.velocity.z};
  v.vec_3c = {c.x70.x, c.x70.y, c.x70.z};
}

// Mech3.exe 1.2 @ 0x004addc0
void vehicle_motion_anim_step(BasicVehicle& v, float throttle) {
  VehBasicHooks& h = hk();
  MotionAnim& a = v.motion;
  if (!(0.9f >= std::fabs(throttle))) {
    a.until = h.game_time() + 0.0f;  // + [0x0060b15c], never set
    if (!a.anim && a.def) a.anim = h.anim_run(a.def, v.node);
    return;
  }
  if (a.anim && h.game_time() > a.until) {
    h.anim_stop(a.anim);  // 0x00506ca0
    a.anim = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x004b4880
void vehicle_weapons_step(BasicVehicle& v, float dt) {
  if (v.ammo) hk().weapons_frame(v.ammo, v.controls->fire_on == 1, dt);
}

// Mech3.exe 1.2 @ 0x004a1040
void vehicle_frame(BasicVehicle& v, float dt) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  const bool ran = c.update(dt) == 1;  // IControls vt+0x0c
  if (c.destroyed()) {
    v.active = false;
    v.engine.entry = nullptr;  // 0x004adb40
    if (v.motion.anim) {  // 0x004ade70
      h.anim_stop(v.motion.anim);
      v.motion.anim = nullptr;
    }
    return;
  }
  if (!ran || !c.on_path()) return;
  h.move(v);
  vehicle_motion_anim_step(v, c.throttle);  // IControls vt+0x1c
  if (const float* p = h.player_lock_position()) c.player_at = {p[0], p[1], p[2]};
  vehicle_weapons_step(v, dt);
  h.refresh(v);
}

// ---------------------------------------------------------------------------
// The collisions (2c4)

namespace {
Vec3c by_matrix(const Vec3c& s, const float* t) {
  return {((s.x * t[0] + s.y * t[3]) + s.z * t[6]) + t[9], ((s.x * t[1] + s.y * t[4]) + s.z * t[7]) + t[10],
          ((s.x * t[2] + s.y * t[5]) + s.z * t[8]) + t[11]};
}
}  // namespace

// Mech3.exe 1.2 @ 0x004a09e0 (one segment's record) within 0x004a0910
bool vehicle_collision_query(BasicVehicle& v, float* segs, int32_t points) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  static zcls::SegmentHits recs[15];
  for (auto& r : recs) r = zcls::SegmentHits{};
  h.node_set_intersect(v.node, 0);
  h.segments(segs, points, recs);
  h.node_set_intersect(v.node, 1);
  for (int32_t i = 0; i < points; i += 2) {
    const zcls::SegmentHits& r = recs[i / 2];
    const float* from = segs + 3 * i;
    for (int j = 0; j < r.count; ++j) {
      const zcls::SegmentHit& hit = r.hit[j];
      std::vector<CollisionHit>* list = nullptr;
      if (h.hit_object(hit.node)) list = &c.objects;
      else if (!(hit.normal[1] >= -0.9f)) list = &c.ceilings;
      else if (!(hit.normal[1] >= 0.71f)) list = &c.walls;
      else if (hit.material && hit.material->soil == 5) {
        // FIX (bug): the exe sets the flag, then adds the crossing to no
        // list: on a segment's first crossing it writes into the segment's
        // own start, on later ones through null (a crash).
        c.x2c = 1;
        continue;
      }
      if (!list) continue;
      CollisionHit e{};
      e.hit = hit;
      e.from = {from[0], from[1], from[2]};
      e.to = {from[3], from[4], from[5]};
      list->push_back(e);
    }
  }
  return c.walls.empty() && c.objects.empty() && c.ceilings.empty();
}

// Mech3.exe 1.2 @ 0x004a0180
void vehicle_collision_sweep(BasicVehicle& v) {
  VehicleControls& c = *v.controls;
  const VehicleMotion& m = c.motion;
  const float s2 = (c.x70.x * c.x70.x + c.x70.y * c.x70.y) + c.x70.z * c.x70.z;
  c.clear = 1;
  int32_t on[15] = {};
  if (!(std::fabs(c.x70.z) >= std::fabs(c.x50.y * 3.3f))) on[0] = on[2] = on[3] = on[5] = on[4] = 1;
  if (s2 > 0.01f) {
    if (c.x70.z >= 0.0f) on[3] = on[5] = 1;
    else on[0] = on[1] = on[2] = 1;
    int32_t side = -1;
    if (c.x70.x > 1.0f) side = 6;
    else if (!(c.x70.x >= -1.0f)) side = 9;
    if (side >= 0) {
      for (int32_t k = side; k < side + 3; ++k) {
        c.x_ac[k] = by_matrix(m.collision[k], v.transform);
        c.points[k] = by_matrix(m.collision[k], c.last_matrix);
        on[k] = 1;
      }
    }
    on[4] = 1;
  }
  float segs[15 * 6];
  int32_t n = 0;
  for (int k = 0; k < 15; ++k) {
    if (!on[k]) continue;
    const Vec3c& a = c.points[k];
    const Vec3c& b = c.x_ac[k];
    float* s = segs + 3 * n;
    s[0] = a.x, s[1] = a.y, s[2] = a.z, s[3] = b.x, s[4] = b.y, s[5] = b.z;
    n += 2;
  }
  if (n) c.clear = vehicle_collision_query(v, segs, n) ? 1 : 0;
}

// Mech3.exe 1.2 @ 0x004a0f50
void vehicle_ceiling_response(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  VehicleMotion& m = c.motion;
  if (c.velocity.y > 0.0f) c.velocity.y = -c.velocity.y;
  float vs[8];
  std::memcpy(vs, m.support_word, sizeof vs);
  for (int32_t i = 0; i < m.support_count && i < 8; ++i)
    if (vs[i] > 0.0f) vs[i] = -vs[i];
  std::memcpy(m.support_word, vs, sizeof vs);
  const float* p = h.entity_position(v);
  const float pos[3] = {p[0], c.last_matrix[10], p[2]};
  h.set_position(v, pos);  // vt+0x18
  for (int32_t i = 0; i < c.point_count; ++i) c.x_ac[i].y = c.points[i].y;
}

// Mech3.exe 1.2 @ 0x0049ff80
void vehicle_collisions(BasicVehicle& v) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  c.hit_object = c.hit_ceiling = c.hit_wall = 0;
  vehicle_collision_sweep(v);
  if (!c.clear) {
    if (!c.objects.empty()) {
      h.wall_response(v, c.objects.back());  // 0x004a1020
      c.hit_object = 1;
    }
    if (!c.ceilings.empty()) {
      vehicle_ceiling_response(v);  // 0x004a0b20
      c.hit_ceiling = 1;
    }
    if (!c.walls.empty()) {
      h.wall_response(v, c.walls.back());  // 0x004a0b00
      c.hit_wall = 1;
    }
    c.objects.clear(), c.ceilings.clear(), c.walls.clear();  // 0x004a0100
  }
  const NetCollision* n = v.net_collision;
  if (c.hit_object || c.hit_wall || !n || !n->on || !n->active) return;
  const uint32_t f = h.frame();
  if (n->frame != f || !(n->sent < f) || !(f - 5 < n->sent)) return;
  CollisionHit e{};
  e.hit.normal[0] = n->normal.x, e.hit.normal[1] = n->normal.y, e.hit.normal[2] = n->normal.z;
  e.hit.point[0] = n->point.x, e.hit.point[1] = n->point.y, e.hit.point[2] = n->point.z;
  e.from = n->point;
  const float* p = h.entity_position(v);
  e.to = {p[0], p[1], p[2]};
  h.wall_response(v, e);
  c.hit_wall = 1;
}

// Mech3.exe 1.2 @ 0x004a0b40
void vehicle_wall_response(BasicVehicle& v, const CollisionHit& hit) {
  VehBasicHooks& h = hk();
  VehicleControls& c = *v.controls;
  zmth::Vec3 n{hit.hit.normal[0], hit.hit.normal[1], hit.hit.normal[2]};
  zmth::Vec3 p{hit.hit.point[0], hit.hit.point[1], hit.hit.point[2]};
  float from[3] = {hit.from.x, hit.from.y, hit.from.z};
  zmth::Vec3 to{hit.to.x, hit.to.y, hit.to.z};
  if (hit.hit.node && h.touchable(hit.hit.node)) h.touch(hit.hit.node, from, hit, 1.0f);
  if ((n.x == 0.0f || std::isnan(n.x)) && (n.z == 0.0f || std::isnan(n.z))) return;
  const float speed = std::sqrt((c.x70.x * c.x70.x + c.x70.y * c.x70.y) + c.x70.z * c.x70.z);
  h.entity_position(v);  // asked, unused
  from[1] = 0.0f, to.y = 0.0f, p.y = 0.0f, n.y = 0.0f;
  const zmth::Vec3 d{to.x - p.x, to.y - p.y, to.z - p.z};
  zmth::flat_normalize(n, n);
  zmth::Vec3 rf;
  zmth::reflect(n, d, &rf);
  const zmth::Vec3 q{p.x + rf.x, p.y + rf.y, p.z + rf.z};
  const zmth::Vec3 push{q.x - to.x, q.y - to.y, q.z - to.z};
  const float* e = h.entity_position(v);
  const float pos[3] = {e[0] + push.x, e[1] + push.y, e[2] + push.z};
  h.set_position(v, pos);  // vt+0x18
  for (int32_t i = 0; i < c.point_count; ++i) c.x_ac[i] = {c.x_ac[i].x + push.x, c.x_ac[i].y + push.y, c.x_ac[i].z + push.z};
  const float vy = c.velocity.y;
  const float bounce = c.motion.collision_d[0];
  zmth::flat_normalize(rf, rf);
  const zmth::Vec3 r{rf.x - n.x, rf.y - n.y, rf.z - n.z};
  const zmth::Vec3 sv{speed * rf.x, speed * rf.y, speed * rf.z};
  const float a = sv.x * r.x + sv.z * r.z;
  const zmth::Vec3 slide{a * r.x, a * r.y, a * r.z};
  const float off = (sv.x * n.x + sv.z * n.z) * bounce;
  const zmth::Vec3 kick{off * n.x, off * n.y, off * n.z};
  c.velocity = {kick.x + slide.x, kick.y + slide.y, kick.z + slide.z};
  c.velocity.y = vy;
  float local[3];
  zmth::rotate_y(local, &c.velocity.x, -c.angles.y);
  c.x70 = {local[0], local[1], local[2]};
  const float side = sv.x * d.z - sv.z * d.x;
  const float s = !(side >= 0.0f) ? -1.0f : 1.0f;
  c.x50.y = (s * speed) * c.motion.collision_d[1] + c.x50.y;
}

}  // namespace mech3

