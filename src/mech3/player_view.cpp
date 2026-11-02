#include "mech3/player_view.h"

#include <cmath>
#include <cstring>

#include "gamez/zclass/cls_di.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zmath/zmth_vector.h"
#include "mech3/mech.h"
#include "mech3/mech_parts.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
ViewHooks* g_hooks = nullptr;

constexpr float kIdentity[4] = {1, 0, 0, 0};  // 0x005eda50
constexpr float kZero[3] = {0, 0, 0};         // 0x0063d870

zmth::Quat& Q(float* q) { return *reinterpret_cast<zmth::Quat*>(q); }
const zmth::Quat& Q(const float* q) { return *reinterpret_cast<const zmth::Quat*>(q); }
zmth::Vec3& V(float* v) { return *reinterpret_cast<zmth::Vec3*>(v); }
const zmth::Vec3& V(const float* v) { return *reinterpret_cast<const zmth::Vec3*>(v); }
void copy4(float* d, const float* s) { std::memcpy(d, s, 16); }
void copy3(float* d, const float* s) { std::memcpy(d, s, 12); }

// 0x005255d0 (ECX a, EDX b, out): a b.
void mul(const float* a, const float* b, float* out) { zmth::quat_mul(Q(a), Q(b), Q(out)); }
// 0x00525500 (ECX q, EDX out)
void normalize(const float* q, float* out) { zmth::quat_normalize(Q(q), Q(out)); }
// 0x00525840 (ECX q, EDX v, out)
void rotate(const float* q, const float* v, float* out) { zmth::quat_rotate(Q(q), V(v), V(out)); }

// Mech3.exe 1.2 @ 0x00525690 (ECX a, EDX b, out): b's conjugate times a, in
// the x87's order of the sums.
void relative(const float* a, const float* b, float* out) {
  out[0] = ((b[3] * a[3] + b[2] * a[2]) + b[1] * a[1]) + b[0] * a[0];
  out[1] = ((b[0] * a[1] - a[0] * b[1]) - b[3] * a[2]) + a[3] * b[2];
  out[2] = ((b[0] * a[2] - a[0] * b[2]) - a[3] * b[1]) + b[3] * a[1];
  out[3] = ((b[0] * a[3] - a[0] * b[3]) - b[2] * a[1]) + a[2] * b[1];
}

// Mech3.exe 1.2 @ 0x00525f80 (ECX q, EDX out): the rotation's Euler angles.
void to_euler(const float* q, float out[3]) {
  zmth::Mat34 m{};
  zmth::quat_matrix(q, m.m);
  zmth::matrix_to_euler(m, out);
}

// The target's centroid node's world position (data +0x84), its stale
// matrix rebuilt first as the camera updates do.
const float* centroid_position(Vehicle& m) {
  ViewHooks& h = *g_hooks;
  if (h.node_flags(m.centroid) & 0x20) h.node_refresh(m.centroid);
  return h.node_position(m.centroid);
}
// The mech camera modes' target: they sit only in a mech's camera list
// (make_cameras), so the view's target is a mech while one is its mode.
Mech& mech_of(PlayerView& v) { return static_cast<Mech&>(*v.target); }
}  // namespace

ViewHooks& view_hooks() { return *g_hooks; }

void ViewHooks::pull_in(float pos[3], const float at[3], const float back[3]) {
  mech3::pull_in(player_target(), pos, at, back);  // [0x005f4c8c] +0x64
}

// Mech3.exe 1.2 @ 0x004a3290 (its file unknown: among the AI's). The search
// for the first hit stops at it, so a water hit after the first keeps its node
// and can still be the nearest (kept).
bool nearest_hit(zcls::SegmentHits* hits, int32_t n, const float from[3], float out[3]) {
  int32_t bi = -1, hi = -1;
  for (int32_t b = 0; b < n && bi < 0; ++b)
    for (int32_t i = 0; i < hits[b].count; ++i) {
      zcls::SegmentHit& h = hits[b].hit[i];
      if (h.material && h.material->soil == 1) {
        h.node = nullptr;
      } else if (h.node) {
        bi = b, hi = i;
        break;
      }
    }
  if (bi < 0) return false;
  const zmth::Vec3 f{from[0], from[1], from[2]};
  auto dist = [&](const zcls::SegmentHit& h) {  // 0x0051dfa0
    return zmth::distance_sq(zmth::Vec3{h.point[0], h.point[1], h.point[2]}, f);
  };
  float best = dist(hits[bi].hit[hi]);
  for (int32_t b = 0; b < n; ++b)
    for (int32_t i = 0; i < hits[b].count; ++i) {
      const zcls::SegmentHit& h = hits[b].hit[i];
      if (!h.node) continue;
      const float d = dist(h);
      if (d < best) best = d, bi = b, hi = i;
    }
  std::memcpy(out, hits[bi].hit[hi].point, 12);
  return true;
}

// Mech3.exe 1.2 @ 0x0040b310. Back: a segment from `back` ([0x005e06f4], 1)
// behind the camera to the look point and the same reversed, the target left
// out of the query; the nearest crossing to the look point puts the camera
// that far in front of it. Sides: two units either way across the look
// (0x00522460, 0x0051e210), the right side's crossing nearest the camera
// first: the camera moved across (x and z) by what that side lacks.
void pull_in(Vehicle* target, float pos[3], const float at[3], const float back[3]) {
  ViewHooks& h = *g_hooks;
  constexpr float k = 1.0f;  // 0x005e06f4
  const float nk = -k;
  const float off[3] = {nk * back[0], nk * back[1], nk * back[2]};
  float segs[12] = {pos[0] + off[0], pos[1] + off[1], pos[2] + off[2], at[0], at[1], at[2],
                    at[0], at[1], at[2], pos[0] + off[0], pos[1] + off[1], pos[2] + off[2]};
  zcls::SegmentHits hits[3]{};  // the exe's 0xf0c bytes on the stack: three records
  Node3 node = target ? target->node : nullptr;
  float hit[3];
  h.node_set_intersect(node, 0);
  h.world_segments(segs, 4, hits);
  h.node_set_intersect(node, 1);
  if (nearest_hit(hits, 2, at, hit)) {
    const float f[3] = {back[0] * k, k * back[1], k * back[2]};
    pos[0] = hit[0] + f[0], pos[1] = hit[1] + f[1], pos[2] = hit[2] + f[2];
  }
  const zmth::Vec3 across{-back[2], 0.0f, back[0]};  // 0x00522460
  zmth::Vec3 n{0.0f, 0.0f, 0.0f};
  zmth::flat_normalize(across, n);
  const float side[3] = {n.x + n.x, 0.0f, n.z + n.z};
  const float right[3] = {pos[0] + side[0], pos[1] + side[1], pos[2] + side[2]};
  const float left[3] = {pos[0] - side[0], pos[1] - side[1], pos[2] - side[2]};
  float segs2[12] = {pos[0], pos[1], pos[2], right[0], right[1], right[2],
                     pos[0], pos[1], pos[2], left[0], left[1], left[2]};
  h.node_set_intersect(node, 0);
  h.world_segments(segs2, 4, hits);
  h.node_set_intersect(node, 1);
  // The sides' nearest is the nearest to the camera (ESI is pos again, 0x0040b488).
  if (nearest_hit(hits, 1, pos, hit)) {
    pos[0] = (hit[0] - right[0]) + pos[0];
    pos[2] = (hit[2] - right[2]) + pos[2];
  } else if (nearest_hit(hits + 1, 1, pos, hit)) {
    pos[0] = (hit[0] - left[0]) + pos[0];
    pos[2] = (hit[2] - left[2]) + pos[2];
  }
}
void set_view_hooks(ViewHooks* h) { g_hooks = h; }

// ---------------------------------------------------------------------------
// Camera modes

// Mech3.exe 1.2 @ 0x0040a950
CameraMode::CameraMode(int32_t i) : id(i) {
  copy4(delta_rot, kIdentity);
  copy3(desired_pos, kZero);
  dist_vary_factor = 0.0f;
  dist = 15.0f;
  dist_factor = 0.1f;
  dist_vary = 0.1f;
  dist_catch_up = 1.0f;
  turn_rate = 2.0f;
  look_rate = 3.0f;
}

// Mech3.exe 1.2 @ 0x0040b8d0: behind the mech at a distance that grows with
// its speed, turning with it.
bool CameraMode::update(float step, PlayerView& v) {
  Vehicle* m = v.target;
  if (!m) return false;
  const float* vel = &m->velocity.x;
  const float s = std::sqrt((vel[0] * vel[0] + vel[1] * vel[1]) + vel[2] * vel[2]);
  const float diff = s - v.speed;
  v.speed = dist_catch_up * diff * step + v.speed;
  const float d = (dist_vary * dist_vary_factor * diff + dist_factor * s) + dist;
  const float* at = centroid_position(*m);
  v.place(at, d, m->orientation, desired_pos, m->orientation, delta_rot);
  return true;
}

// Mech3.exe 1.2 @ 0x0040b820
void CameraMode::reset(PlayerView& v) {
  Vehicle* m = v.target;
  if (!m) return;
  const float* vel = &m->velocity.x;
  v.speed = std::sqrt((vel[0] * vel[0] + vel[1] * vel[1]) + vel[2] * vel[2]);
  const float* at = centroid_position(*m);
  v.place_now(at, dist, m->orientation, desired_pos, m->orientation, delta_rot);
}

// Mech3.exe 1.2 @ 0x00421530
MechCamera::MechCamera(int32_t i) : CameraMode(i) {
  copy4(look_q, kIdentity);
  look_left = zgame::from_bits(0x3fc6d3f2);
  look_right = zgame::from_bits(0xbfc6d3f2);
  look_down = zgame::from_bits(0xbe860a92);
  look_up = zgame::from_bits(0x3f490fdb);
  dist_now = dist;
}

// Mech3.exe 1.2 @ 0x004215d0: the camera's block: dist, the speed terms,
// desired_pos, delta_rot (degrees) and look_limits (degrees).
bool MechCamera::read(void* node) {
  if (!node) return false;
  ViewHooks& h = *g_hooks;
  float e[3] = {kZero[0], kZero[1], kZero[2]};
  h.reader_float(node, "dist", &dist);
  h.reader_float(node, "dist_vary_factor", &dist_vary_factor);
  h.reader_float(node, "dist_vary", &dist_vary);
  h.reader_float(node, "dist_factor", &dist_factor);
  h.reader_float(node, "dist_catch_up", &dist_catch_up);
  h.reader_vec3(node, "desired_pos", desired_pos);
  if (h.reader_vec3(node, "delta_rot", e)) {
    constexpr float k = 0.017453292f;  // 0x005bd320
    e[0] = e[0] * k, e[1] = e[1] * k, e[2] = e[2] * k;
    zmth::euler_to_quat(e[1], e[0], e[2], delta_rot);
  }
  if (void* lim = h.reader_find(node, "look_limits")) {
    constexpr double k = 0.01745329251994;  // 0x005bd328
    auto deg = [&](const char* key, float* f) {
      if (h.reader_float(lim, key, f)) *f = static_cast<float>(*f * k);
    };
    deg("left", &look_left);
    deg("right", &look_right);
    deg("up", &look_up);
    deg("down", &look_down);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00421760: tether and the like. The speed drops slowly and
// rises at once; the distance never below 0.9 x dist.
bool MechCamera::update(float step, PlayerView& v) {
  Mech& m = mech_of(v);
  const float a = std::fabs(m.vec_3c.z);
  const float da = a - speed;
  if (da < 0.0f) speed = speed - da * step * -0.15f;  // 0x005bd330, 0x005bd334
  else speed = a;
  const float diff = speed - v.speed;
  v.speed = dist_catch_up * diff * step + v.speed;
  const float least = dist * 0.9f;  // 0x005bd338
  float d = (dist_factor * speed + dist_vary * diff) + dist;
  if (!(d > least)) d = least;
  float q[4];
  if (torso_look) {
    mul(m.orientation, g_hooks->torso_aim(m), q);
    normalize(q, q);
  } else {
    copy4(q, m.orientation);
  }
  float off[3];
  copy3(off, desired_pos);
  if (look_q[0] != 1.0f) {
    float r[4];
    mul(q, look_q, r);
    copy4(q, r);
  }
  const float* at = centroid_position(m);
  v.place(at, d, q, off, q, delta_rot);
  return true;
}

// Mech3.exe 1.2 @ 0x00421900: as the update, without the smoothing.
void MechCamera::reset(PlayerView& v) {
  Mech& m = mech_of(v);
  const float a = std::fabs(m.vec_3c.z);
  speed = a;
  v.speed = a;
  float q[4];
  if (torso_look) {
    mul(m.orientation, g_hooks->torso_aim(m), q);
    normalize(q, q);
  } else {
    copy4(q, m.orientation);
  }
  float off[3];
  copy3(off, desired_pos);
  if (look_q[0] != 1.0f) {
    float r[4];
    mul(q, look_q, r);
    copy4(q, r);
  }
  const float* at = centroid_position(m);
  v.place_now(at, dist, q, off, q, delta_rot);
}

// Mech3.exe 1.2 @ 0x00421e40: in the cockpit, at the torso's node, turned with
// the torso (its own aim when the AI holds the view), with the head's bob.
bool CockpitCamera::update(float step, PlayerView& v) {
  Mech& m = mech_of(v);
  const float* vel = &m.velocity.x;
  const float s = std::sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
  const float diff = s - v.speed;
  v.speed = dist_catch_up * diff * step + v.speed;
  const float d = (dist_vary * dist_vary_factor * diff + dist_factor * s) + dist;
  const float* aim = g_hooks->torso_aim(m);
  const float* held = m.torso->view_aim ? m.torso->view_q : nullptr;
  float q[4], at[3];
  float off[3] = {};  // FIX (bug): left unset in the original when d is not 0 (docs/view.md)
  if (!fixed) {
    if (torso_look) {
      mul(m.orientation, held ? held : aim, q);
      normalize(q, q);
    } else {
      copy4(q, m.orientation);
    }
    g_hooks->node_world_position(m.torso->node, at);
  } else {
    copy3(at, kZero);
    copy4(q, kIdentity);
  }
  if (d == 0.0f) {
    if (!held) {
      rotate(q, desired_pos, off);
    } else {
      float q2[4];
      mul(m.orientation, aim, q2);
      normalize(q2, q2);
      rotate(q2, desired_pos, off);
    }
    if (g_hooks->head_bob()) {
      constexpr float k = -0.2f;      // 0x005bd348
      const float s15 = g_hooks->head_bob_scale();  // [0x005e244c]
      float b[3] = {m.vec_58.x, m.vec_58.y, m.vec_58.z};
      b[1] = b[1] - g_hooks->torso_pitch_rate(m) * k;
      b[0] = s15 * (b[0] - g_hooks->torso_yaw_rate(m) * k);
      b[1] = s15 * b[1];
      b[2] = s15 * b[2];
      float bq[4], r[4];
      zmth::quat_exp(V(b), Q(bq));
      mul(q, bq, r);
      copy4(q, r);
    }
  }
  if (!held && look_q[0] != 1.0f) {
    float r[4];
    mul(q, look_q, r);
    copy4(q, r);
  }
  v.place(at, d, q, off, q, delta_rot);
  return true;
}

// Mech3.exe 1.2 @ 0x00421d40
void CockpitCamera::reset(PlayerView& v) { MechCamera::reset(v); }

// Mech3.exe 1.2 @ 0x004221d0: like the base, turned by the look.
bool AreaCamera::update(float step, PlayerView& v) {
  Mech& m = mech_of(v);
  const float* vel = &m.velocity.x;
  const float s = std::sqrt(vel[2] * vel[2] + vel[0] * vel[0]);
  const float diff = s - v.speed;
  v.speed = dist_catch_up * diff * step + v.speed;
  float q[4];
  copy4(q, m.orientation);
  if (look_q[0] != 1.0f) {
    float r[4];
    mul(q, look_q, r);
    copy4(q, r);
  }
  const float* at = centroid_position(m);
  const float d = (dist_factor * s + dist_vary_factor * dist_vary * diff) + dist;
  v.place(at, d, q, desired_pos, q, delta_rot);
  return true;
}

// ---------------------------------------------------------------------------
// The camera list (mech_data.cpp)

CameraMode* CameraList::find(int32_t id) const {
  for (CameraMode* c : modes)
    if (c->id == id) return c;
  return nullptr;
}
CameraList::~CameraList() {
  for (CameraMode*& c : modes) {
    delete c;
    c = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x00430e70
CameraList* make_cameras(int32_t roster) {
  ViewHooks& h = *g_hooks;
  auto* list = new CameraList;
  void* cams = h.reader_find(h.dfn_reader(roster), "camera");
  auto block = [&](const char* name) { return h.reader_find(cams, name); };

  auto* tether = new MechCamera(0);
  tether->dist = 12.0f;
  copy4(tether->delta_rot, kIdentity);
  zmth::quat_pitch(zgame::from_bits(0xbdb2b8c2), Q(tether->delta_rot));
  switch (roster) {
    case 0: tether->desired_pos[1] = 0.7f; break;
    case 1: case 0x10: tether->desired_pos[1] = 0.6f; break;
    case 2: case 4: case 0xd: tether->desired_pos[1] = 0.55f; break;
    case 6: case 9: case 10: case 0xf: case 0x11: tether->desired_pos[1] = 0.65f; break;
    case 7: case 8: case 0xc: case 0xe: tether->desired_pos[1] = 0.45f; break;
    case 0xb: tether->desired_pos[1] = 0.42f; break;
    case 0x12: tether->desired_pos[1] = 0.63f; break;
    default: tether->desired_pos[1] = 0.5f; break;
  }
  tether->desired_pos[2] = 0.93f;
  tether->desired_pos[0] = 0.0f;
  zmth::normalize(V(tether->desired_pos));
  tether->dist_vary_factor = 1.0f;
  tether->dist_vary = 0.5f;
  tether->dist_factor = 0.0f;
  tether->dist_catch_up = 0.4f;
  tether->read(block("tether"));
  list->modes.push_back(tether);

  auto* attach = new CockpitCamera;
  attach->dist = 0.0f;
  copy4(attach->delta_rot, kIdentity);
  attach->desired_pos[0] = 0.0f;
  attach->desired_pos[1] = 2.0f;
  attach->desired_pos[2] = 0.0f;
  attach->dist_vary_factor = 0.0f;
  attach->dist_vary = 0.0f;
  attach->dist_factor = 0.0f;
  attach->look_rate = 4.0f;
  attach->turn_rate = 2.0f;
  attach->read(block("attach"));
  list->modes.push_back(attach);

  auto* zipper = new MechCamera(2);
  zipper->dist = 14.0f;
  copy4(zipper->delta_rot, kIdentity);
  zmth::quat_pitch(zgame::from_bits(0x3d0efa35), Q(zipper->delta_rot));
  zipper->desired_pos[1] = -0.12f;
  zipper->desired_pos[2] = 1.0f;
  zipper->desired_pos[0] = 0.36f;
  zmth::normalize(V(zipper->desired_pos));
  zipper->dist_vary_factor = 1.0f;
  zipper->dist_vary = 0.5f;
  zipper->dist_factor = 0.0f;
  zipper->dist_catch_up = 0.4f;
  zipper->read(block("zippercam"));
  list->modes.push_back(zipper);

  auto* gods = new MechCamera(3);
  gods->dist = 150.0f;
  zmth::quat_pitch(zgame::from_bits(0xbfc90fd8), Q(gods->delta_rot));
  gods->desired_pos[0] = 0.0f;
  gods->desired_pos[1] = 1.0f;
  gods->desired_pos[2] = 0.0f;
  gods->dist_vary_factor = 0.0f;
  gods->dist_vary = 0.0f;
  gods->dist_factor = 0.0f;
  gods->read(block("godseye"));
  list->modes.push_back(gods);

  auto* area = new AreaCamera;
  float fov = 0.0f;
  const double fx = h.view_fov(&fov) ? double{fov} : 1.0471975511964;
  const float half = static_cast<float>(fx * 0.5);
  area->desired_pos[0] = 0.0f;
  area->desired_pos[2] = 1.0f;
  area->dist_vary_factor = 1.0f;
  area->dist = 40.0f;
  area->desired_pos[1] = static_cast<float>(std::tan(double{half}));
  area->read(block("area"));
  list->modes.push_back(area);

  auto* mcb = new MechCamera(4);
  mcb->dist = 30.0f;
  copy4(mcb->delta_rot, kIdentity);
  zmth::quat_pitch(zgame::from_bits(0xbe20d97c), Q(mcb->delta_rot));
  mcb->desired_pos[2] = 1.0f;
  mcb->dist_vary_factor = 1.0f;
  mcb->desired_pos[0] = 0.0f;
  mcb->dist_factor = 0.0f;
  mcb->desired_pos[1] = 0.2f;
  mcb->dist_vary = 0.2f;
  mcb->dist_catch_up = 0.2f;
  mcb->read(block("mcbtether"));
  list->modes.push_back(mcb);
  return list;
}

// ---------------------------------------------------------------------------
// The view

// Mech3.exe 1.2 @ 0x0040aca0 (with zView's 0x004b5060 the program's window
// and the rest): the horizon node set on the camera.
PlayerView::PlayerView(Node3 cam, Node3 win, Node3 x, int32_t hw) : camera(cam), window(win), x0c(x), hardware(hw) {
  g_hooks->camera_set_horizon(camera, g_hooks->find_node(6, "horizon"));
}

// Mech3.exe 1.2 @ 0x0040bab0
void PlayerView::add_to_camera(Node3 node) { g_hooks->node_add_child(camera, node); }

// Mech3.exe 1.2 @ 0x0040ada0
void PlayerView::update(float step) {
  dt = step;
  handled = 0;
  if (mode) mode->update(step, *this);
  if (!handled && mode && target) follow_ground();
  weather_check();
}

// Mech3.exe 1.2 @ 0x0040adf0: the old target and mode let go, the new taken,
// then one update with the last frame time.
bool PlayerView::attach(Vehicle* t, CameraMode* m) {
  if (target) {
    if (mode) mode->deactivate(*this);
    g_hooks->target_detached(*target);
  }
  target = t;
  mode = m;
  if (t) {
    g_hooks->target_attached(*t);
    if (m) m->activate(*this);
  } else {
    mode = nullptr;
  }
  update(dt);
  return true;
}

// Mech3.exe 1.2 @ 0x0040ae70: to the target's camera `id` when it may be used.
void PlayerView::set_camera_mode(int32_t id) {
  CameraMode* m = target ? g_hooks->target_camera(*target, id) : nullptr;
  if (!m || !m->usable(*this)) return;
  if (mode) {
    last_mode = mode->id;
    mode->deactivate(*this);
  }
  mode = m;
  m->activate(*this);
}

// Mech3.exe 1.2 @ 0x0040ad90
void PlayerView::reset() {
  if (mode) mode->reset(*this);
}

// Mech3.exe 1.2 @ 0x0040b7e0
void PlayerView::place(const float at[3], float d, const float rot[4], const float offset[3], const float look[4],
                       const float delta[4]) {
  place_position(at, d, rot, offset);
  place_look(look, delta);
}

// Mech3.exe 1.2 @ 0x0040b600: at `at` plus the offset turned by the smoothed
// rotation, `d` away; the outside cameras pulled in before walls (0x0040b310).
void PlayerView::place_position(const float at[3], float d, const float rot[4], const float offset[3]) {
  dist = d;
  if (d == 0.0f) {
    pos[0] = at[0] + offset[0];
    pos[1] = at[1] + offset[1];
    pos[2] = at[2] + offset[2];
  } else {
    float rel[4], step_q[4];
    relative(rot, rot_q, rel);
    zmth::quat_scale(Q(rel), dt * mode->turn_rate, Q(step_q));
    mul(step_q, rot_q, rel);
    normalize(rel, rot_q);
    rotate(rot_q, offset, pos);
    float back[3] = {pos[0] * -1.0f, pos[1] * -1.0f, pos[2] * -1.0f};
    const float s = dist;
    pos[0] = s * pos[0];
    pos[1] = pos[1] * s;
    pos[2] = pos[2] * s;
    pos[0] = pos[0] + at[0];
    pos[1] = pos[1] + at[1];
    pos[2] = pos[2] + at[2];
    if (mode->id == 0 || mode->id == 2 || mode->id == 5) g_hooks->pull_in(pos, at, back);  // 0x0040b310
  }
  g_hooks->camera_set_position(camera, pos[0], pos[1], pos[2]);
}

// Mech3.exe 1.2 @ 0x0040b750: the look turned toward `look` at dt x the
// mode's rate, then the mode's delta, as the camera's angles.
void PlayerView::place_look(const float look[4], const float delta[4]) {
  float rel[4], step_q[4], r[4];
  relative(look, look_q, rel);
  zmth::quat_scale(Q(rel), dt * mode->look_rate, Q(step_q));
  mul(step_q, look_q, r);
  normalize(r, look_q);
  mul(look_q, delta, final_q);
  float e[3];
  to_euler(final_q, e);
  g_hooks->camera_set_rotation(camera, e[0], e[1], e[2]);
}

// Mech3.exe 1.2 @ 0x0040b210
void PlayerView::place_now(const float at[3], float d, const float rot[4], const float offset[3], const float look[4],
                           const float delta[4]) {
  const float a[3] = {at[0], at[1], at[2]};
  dist = d;
  copy4(rot_q, rot);
  rotate(rot_q, offset, pos);
  pos[0] = pos[0] * d;
  pos[1] = pos[1] * d;
  pos[2] = pos[2] * d;
  pos[0] = pos[0] + a[0];
  pos[1] = pos[1] + a[1];
  pos[2] = pos[2] + a[2];
  g_hooks->camera_set_position(camera, pos[0], pos[1], pos[2]);
  copy4(look_q, look);
  mul(look_q, delta, final_q);
  float e[3];
  to_euler(final_q, e);
  g_hooks->camera_set_rotation(camera, e[0], e[1], e[2]);
}


// ---------------------------------------------------------------------------
// The mech cameras' switching

// Mech3.exe 1.2 @ 0x00421ad0: the cockpit takes the torso's look and
// parents the camera to the torso while the mech is off; the outside ones
// give the torso back to the controls; godseye and area look straight.
void MechCamera::activate(PlayerView& v) {
  Mech& m = mech_of(v);
  ViewHooks& h = *g_hooks;
  view = &v;
  switch (id) {
    case 1:
      h.torso_cockpit(m, 1);
      torso_look = 1;
      h.mech_41ca60(m, 0);
      h.controls_426780(m, h.option_5f4d44());
      h.anim_502da0(1);
      if (m.active && !(m.anim_state == 12 || m.anim_state == 13)) return;
      if (v.mode) v.mode->on_switch(v, true);
      return;
    case 0:
    case 2:
      h.torso_cockpit(m, 0);
      if (h.controls_flags(m) & 4) {
        h.torso_466190(m, 1);
        torso_look = 1;
        h.anim_502da0(0);
        return;
      }
      h.controls_426780(m, h.option_5f4d44());
      h.anim_502da0(0);
      return;
    case 3:
    case 5:
      h.torso_cockpit(m, 0);
      view->mode->set_look(2);
      return;
    default: return;
  }
}

// Mech3.exe 1.2 @ 0x00421a30: the outside cameras are barred when the
// pilot's game says so (+0x8e); 0 and 2 ask twice, as written.
bool MechCamera::usable(PlayerView& v) {
  if (!v.target) return false;
  ViewHooks& h = *g_hooks;
  auto barred = [&] {
    if (!h.pilot_game()) return false;
    return h.game_byte_8e(h.pilot_game()) != 0;
  };
  if (id == 1) return true;
  if ((id == 0 || id == 2) && barred()) return false;
  return !barred();
}

// Mech3.exe 1.2 @ 0x00421c00: the free look (0 left, 1 right, 2 ahead, 3
// none, 4 up, 5 down) as the look's rotation, told to the HUD.
void MechCamera::set_look(int32_t look) {
  ViewHooks& h = *g_hooks;
  Mech* m = view->target ? &mech_of(*view) : nullptr;
  if (!m || h.mech_dead(*m) != 0 || !m->active) return;
  h.torso_464ac0(*m, 0);
  switch (look) {
    case 0:
      zmth::quat_yaw(look_left, Q(look_q));
      h.torso_464ac0(*m, 1);
      break;
    case 1:
      zmth::quat_yaw(look_right, Q(look_q));
      h.torso_464ac0(*m, 1);
      break;
    case 2:
      copy4(look_q, kIdentity);
      h.torso_465a00(*m, 1);
      break;
    case 4: zmth::quat_pitch(look_up, Q(look_q)); break;
    case 5: zmth::quat_pitch(look_down, Q(look_q)); break;
    default: break;
  }
  const uint32_t screen = m == h.player_target() ? h.mech_screen(*m) : 0;
  if (screen) {
    if (m == h.player_target()) h.screen_look(h.mech_screen(*m));
    else h.screen_look(0);
  }
  look_ = look;
}

// Mech3.exe 1.2 @ 0x004220c0: as the mech cameras', in quarter turns; the
// look's number is not kept.
void AreaCamera::set_look(int32_t look) {
  ViewHooks& h = *g_hooks;
  Mech& m = mech_of(*view);
  if (h.mech_dead(m) != 0) return;
  float angle;
  switch (look) {
    case 0: angle = zgame::from_bits(0x3fc90fdb); break;
    case 1: angle = zgame::from_bits(0xbfc90fdb); break;
    case 3: angle = zgame::from_bits(0x40490fdb); break;
    case 2:
    case 4:
      copy4(look_q, kIdentity);
      h.mech_41ca30(m, look == 4);
      return;
    case 5:
      zmth::quat_pitch(zgame::from_bits(0xbf060a92), Q(look_q));
      h.mech_41ca30(m, 1);
      return;
    default: return;
  }
  zmth::quat_yaw(angle, Q(look_q));
  h.mech_41ca30(m, 1);
  h.torso_465a00(m, 1);
}

// Mech3.exe 1.2 @ 0x00421d50: on, the camera node becomes the torso node's
// [0x00605624], read only by the two below and never written (a zeroed
// byte): the camera-switch branch is dead in 1.2.
constexpr bool kFallSwitchesMode = false;

// Mech3.exe 1.2 @ 0x00413af0
void PlayerView::mech_falls(Mech& m) {
  if (&m != target) return;
  view_hooks().torso_465a00(m, 1);  // 0x00465a00(1) on its +0x39c
  if (kFallSwitchesMode) {
    set_camera_mode(mode && mode->id == 1 ? 2 : mode ? mode->id : -1);
    return;
  }
  if (mode) mode->on_switch(*this, true);
}

// Mech3.exe 1.2 @ 0x00413b70
void PlayerView::mech_stands(Mech& m) {
  if (&m != target) return;
  if (kFallSwitchesMode) {
    set_camera_mode(last_mode);
    return;
  }
  if (mode) mode->on_switch(*this, false);
}

// child at the cockpit's offset; off, the world's again, put behind as reset.
void CockpitCamera::on_switch(PlayerView& v, bool on) {
  if (static_cast<uint8_t>(on) == fixed) return;
  ViewHooks& h = *g_hooks;
  Mech& m = mech_of(v);
  const Node3 parent = h.node_parent(v.camera);
  if (on) {
    h.node_remove_child(parent, v.camera);
    h.node_add_child(m.torso->node, v.camera);
    v.place_now(desired_pos, dist, kIdentity, kZero, kIdentity, kIdentity);
    if (h.controls_kind(m) == 1) h.controls_426780(m, 0);
  } else {
    h.node_remove_child(parent, v.camera);
    h.node_add_child(h.world_node(), v.camera);
    float off[3];
    rotate(m.orientation, desired_pos, off);
    v.place_now(desired_pos, dist, m.orientation, off, m.orientation, delta_rot);
  }
  fixed = on;
}

// ---------------------------------------------------------------------------
// The view's frame checks

// Mech3.exe 1.2 @ 0x0040aed0: the camera key: attach, then (when allowed)
// zippercam, else tether.
void PlayerView::cycle_camera() {
  if (!mode) return;
  ViewHooks& h = *g_hooks;
  switch (mode->id) {
    case 0: return h.set_camera_option(1);
    case 1:
      if (h.pilot_game() && h.game_byte_8e(h.pilot_game())) return;
      return h.set_camera_option(2);
    default: return h.set_camera_option(0);
  }
}

// Mech3.exe 1.2 @ 0x0040af30: the ground and water under the camera: under
// water the world takes the water's fog; an outside camera below the ground
// is lifted half a metre above it; the camera's zone from the ground's hit.
bool PlayerView::follow_ground() {
  ViewHooks& h = *g_hooks;
  float p[3];
  h.node_world_position(camera, p);
  if (h.ground_query(p[0], p[1] - -50.0f, p[2])) return false;  // 0x005bc2f4
  float height = 12.0f;
  if (target) height = target->height - (p[1] - h.entity_position(*target)[1]);
  int32_t index = 0, kind = 0;
  float level = 0.0f;
  const float ground = h.water_query(p[1], height, &index, h.water_flag(), &kind, &level);
  bool under = false;
  if (kind == 1 && p[1] < level && level > ground) under = true;
  set_underwater(under);
  const bool cockpit = mode && mode->id == 1;
  if (!cockpit && p[1] < ground) h.camera_add_position(camera, 0.0f, (ground - p[1]) - -0.5f, 0.0f);  // 0x005bc300
  h.camera_zone(camera, index, target);
  return true;
}

// Mech3.exe 1.2 @ 0x0040b0e0: the world's fog, the water's or the air's
// (world.zrd's fog blocks, mission_world.h).
void PlayerView::set_underwater(bool on) {
  if (underwater == static_cast<uint8_t>(on)) return;
  ViewHooks& h = *g_hooks;
  const float* f = h.mission_fog(on);  // range 2, colour 3, altitude 2
  h.world_fog_range(f[0], f[1]);
  h.world_fog_color(f[4], f[5], f[6]);
  h.world_fog_altitude(f[2], f[3]);
  underwater = on;
}

// Mech3.exe 1.2 @ 0x0040b9a0: the weather hidden while something is over
// the camera (100 m up), shown again in the open.
void PlayerView::weather_check() {
  ViewHooks& h = *g_hooks;
  Vehicle* t = h.player_target();
  if (!t || !h.has_weather()) return;
  h.node_set_intersect(t->node, 0);
  h.query_mode(1);
  const float* c = h.camera_position(camera);
  const float a[3] = {c[0], c[1], c[2]};
  const float b[3] = {c[0] + 0.0f, c[1] + 100.0f, c[2] + 0.0f};  // 0x005e06f8
  uint8_t found = 0;
  const int32_t r = h.segment_query(a, b, &found);
  h.query_mode(0);
  h.node_set_intersect(t->node, 1);
  const bool hidden = (h.weather_flags() >> 4) & 1;
  if (r == 0 && found > 0) {
    if (!hidden) h.weather_hide();
  } else if (hidden) {
    h.weather_show();
  }
}

}  // namespace mech3
