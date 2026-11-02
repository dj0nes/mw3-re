// The pose on a mech's parts: each part's set_pose, the leg IK (docs/movement-pose.md).
// Original file: mech3\mech_parts.cpp (the part methods, 0x00460280..0x00463ee0).
#include <cmath>

#include "crt/msvc_math.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "mech3/mech.h"
#include "gamez/zutil/zfix.h"
#include "mech3/mech_anim.h"
#include "mech3/mech_move.h"
#include "mech3/mech_skis.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
PoseCalls g_pose;
bool g_leg_roll = true;  // [0x005e4f1c], 1 in the data: the thigh also rolls toward the foot's side
}  // namespace

PoseCalls& pose_calls() { return g_pose; }

// Mech3.exe 1.2 @ 0x00460180
void MechPart::node_changed() {
  if (g_pose.node_set) g_pose.node_set(node, rotation);
}

// Mech3.exe 1.2 @ 0x00460280. A pending pose wins once over the one given
// (whose pointers only say which halves to set).
void MechPart::set_pose(const Vec3* pos, const float* q) {
  if (!node) return;
  if (pending) {
    if (pos) *translation = pending_pos;
    if (q) zmth::quat_matrix(pending_q, rotation);
    MechPart::node_changed();
    pending = false;
    pending_pos = {0, 0, 0};                                  // 0x0063d870
    pending_q[0] = 1, pending_q[1] = pending_q[2] = pending_q[3] = 0;  // 0x005eda50
    return;
  }
  if (pos) *translation = *pos;
  if (q) zmth::quat_matrix(q, rotation);
  node_changed();
}

// Mech3.exe 1.2 @ 0x004629b0. The rotation waits for the foot's update.
void Foot::set_pose(const Vec3* pos, const float* q) {
  *translation = *pos;
  if (q)
    for (int i = 0; i < 4; ++i) pending_q[i] = q[i];
}

// Mech3.exe 1.2 @ 0x004624b0: the offset (the crouch) added; a new rotation
// also keeps its inverse and puts both feet back on the ground (0x00462a10).
void Hip::set_pose(const Vec3* pos, const float* q) {
  const Vec3 p{offset.x + pos->x, offset.y + pos->y, offset.z + pos->z};
  MechPart::set_pose(&p, q);
  if (!q) return;
  for (int i = 0; i < 4; ++i) rot[i] = q[i];
  zmth::Quat c;
  zmth::quat_conjugate(zmth::Quat{q[0], q[1], q[2], q[3]}, c);
  zmth::quat_normalize(c, c);
  inv[0] = c.w, inv[1] = c.x, inv[2] = c.y, inv[3] = c.z;
  foot_ground(*mech->legs[0]->foot);
  foot_ground(*mech->legs[1]->foot);
}

// Mech3.exe 1.2 @ 0x00464600. Only the rotation, kept for the torso's
// update. In the cockpit view the player's torso keeps a tenth of the
// animation's sway, taken out of the hip's turn.
void Torso::set_pose(const Vec3*, const float* q) {
  if (!q) return;
  const int view = damage_calls().view_mode ? damage_calls().view_mode() : -1;
  if (view == 1 && mech == world().player) {
    zmth::Quat t, r;
    zmth::quat_scale(zmth::Quat{q[0], q[1], q[2], q[3]}, 0.1f, t);  // 0x005e4f24
    const float* inv = mech->hip->inv;
    zmth::quat_mul(zmth::Quat{inv[0], inv[1], inv[2], inv[3]}, t, r);
    zmth::quat_normalize(r, r);
    rot[0] = r.w, rot[1] = r.x, rot[2] = r.y, rot[3] = r.z;
    return;
  }
  for (int i = 0; i < 4; ++i) rot[i] = q[i];
}

// Mech3.exe 1.2 @ 0x00463c90. Two-bone IK in the leg's plane: the knee from
// the law of cosines (an ankle out of reach is pulled in along the line to
// it), the thigh's pitch, and its roll toward the ankle's side. fpatan and
// its products stay on the x87 stack; the 24-bit rounding is kept.
bool Leg::solve(const Vec3& p) {
  const Vec3& t = *translation;
  float y = -(p.y - t.y);
  float z = -(p.z - t.z);
  const float d2 = y * y + z * z;
  const float xr = p.x - hip_x;
  const float s2 = ik_k + d2;
  float c = ik_m * s2;
  if (!(c <= 1.0f) || c < -1.0f) {  // NaN is pulled in too
    const float u = 1.0f / c - ik_k / s2;
    const float k = std::sqrt(u * s2 / d2);
    c = 1.0f;
    y = y * k;
    z = k * z;
  }
  const double roll = std::atan2(double{xr}, double{y});
  float s = std::sqrt(1.0f - c * c);
  if (!knee_back) s = -s;
  knee_angle = static_cast<float>(std::atan2(double{s}, double{c}));
  const double b = std::atan2(double{z}, double{y});
  const float den = calf_len * c + thigh_len;
  const double a = std::atan2(double{calf_len * s}, double{den});
  thigh_angle = crt::x87_add_f24(b, -a);
  zmth::Quat pitch, out;
  zmth::quat_pitch(thigh_angle, pitch);
  if (g_leg_roll) {
    zmth::Quat r;
    zmth::quat_roll(static_cast<float>(roll), r);
    zmth::quat_mul(r, pitch, out);
  } else {
    out = pitch;
  }
  thigh_q[0] = out.w, thigh_q[1] = out.x, thigh_q[2] = out.y, thigh_q[3] = out.z;
  zmth::quat_pitch(knee_angle, out);
  calf_q[0] = out.w, calf_q[1] = out.x, calf_q[2] = out.y, calf_q[3] = out.z;
  return true;
}

// Mech3.exe 1.2 @ 0x00463e40. p is in the hip's frame without its offset;
// the foot gets it and its rotation only when the solve succeeds (always).
bool Leg::reach(const Vec3& p, const float q[4]) {
  const Vec3& o = mech->hip->offset;
  const Vec3 local{p.x - o.x, p.y - o.y, p.z - o.z};
  const bool ok = solve(local);
  set_pose(nullptr, thigh_q);
  calf->set_pose(nullptr, calf_q);
  if (!ok) return false;
  foot->set_pose(&local, q);
  return true;
}

// Mech3.exe 1.2 @ 0x00460210: the pose made pending, then applied through
// the base set_pose (no hip offset, no foot target).
void MechPart::place(const Vec3* pos, const float* q) {
  if (destroyed || !node) return;
  pending_pos = pos ? *pos : *translation;
  for (int i = 0; i < 4; ++i) pending_q[i] = q[i];
  pending = true;
  static const Vec3 zero{0, 0, 0};                 // 0x0063d870
  static const float identity[4] = {1, 0, 0, 0};  // 0x005eda50
  MechPart::set_pose(&zero, identity);
}

namespace {
float* node_matrix(Handle n) { return g_pose.node_matrix ? g_pose.node_matrix(n) : nullptr; }

void slerp_into(const float* from, float* q, float t) { zmth::quat_slerp(from, q, t, q); }

void lerp_into(const Vec3& from, Vec3& p, float t, bool y) {
  p.x = (p.x - from.x) * t + from.x;
  if (y) p.y = (p.y - from.y) * t + from.y;
  p.z = (p.z - from.z) * t + from.z;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00417c65..0x00417cd2: walk or faster, no jets, the
// first 20 frames since stand or step. FIX (B): counted in 1/30 s steps.
float Mech::gait_blend(int jets) const {
  const float done = zfix::options().per_second ? gait_steps : static_cast<float>(gait_frames);
  if (!(anim_state >= 4 && done < 20.0f && jets == 0)) return -1.0f;  // 0x005e1fac
  const float b = done / 20.0f;
  return b > 1.0f ? 1.0f : b < 0.15f ? 0.15f : b;  // 0x005e1fb0
}

// The toe loops of the three poses: the left foot's toe count for both feet.
// FIX (bug): the exe lets the right foot's toe index equal its count (ja),
// past its arrays when it has fewer toes than the left; here it stops.
void Mech::pose_toes(MechAnim& a, float blend) {
  const uint32_t n = legs[0]->foot->toe_count;
  for (uint32_t i = 0; i < n; ++i)
    for (int leg = 0; leg < 2; ++leg) {
      float q[4];
      if (!a.toe(leg, static_cast<int>(i), q)) continue;
      Foot& f = *legs[leg]->foot;
      if (zfix::options().bugs ? i >= f.toe_count : i > f.toe_count) continue;  // off: the exe's, for the tests
      if (blend != 1.0f) slerp_into(f.toe_q[i], q, blend);
      for (int k = 0; k < 4; ++k) f.toe_q[i][k] = q[k];
      float* m = node_matrix(f.toe_nodes[i]);
      if (!m) continue;
      zmth::quat_matrix(q, m);
      if (g_pose.node_set) g_pose.node_set(f.toe_nodes[i], m);
    }
}

// Mech3.exe 1.2 @ 0x00417bbc..0x0041818d (Move). The feet sampled in their
// skis' frames; over the blend-in the feet, hip, torso and root motion go
// from the last pose toward the animation's. The exe blends the left foot's
// rotation twice (from each foot's last) and the right's never.
void Mech::pose_gait(MechAnim& a, int jets, const Vec3& saved) {
  float* (*ski)(Handle) = skis_calls().node_matrix;
  a.ski_frame(0, ski ? ski(skis->ski[0]) : nullptr);
  a.ski_frame(1, ski ? ski(skis->ski[1]) : nullptr);
  Vec3 hp{0, 0, 0}, lp, rp;
  float hq[4], lq[4], rq[4], tq[4];
  a.hip(&hp.x, hq);
  hp.z = 0.0f;
  a.foot(0, &lp.x, lq);
  a.foot(1, &rp.x, rq);
  a.torso_rotation(tq);
  float blend = 1.0f;
  const float crouch = hip->offset.y;
  const float b = gait_blend(jets);
  if (b >= 0.0f) {
    for (int i = 0; i < 2; ++i) {
      Foot& f = *foot(i);
      lerp_into(*f.translation, i == 0 ? lp : rp, b, crouch == 0.0f);
      // FIX (bug): each foot from its own last rotation (Owen, 2026-10-20);
      // the exe writes both into the left's, so the right foot snaps.
      slerp_into(f.rot, zfix::options().bugs && i == 1 ? rq : lq, b);
    }
    slerp_into(hip->rot, hq, b);
    lerp_into(*hip->translation, hp, b, crouch == 0.0f);
    lerp_into(saved, vec_3c, b, true);
    slerp_into(torso->pending_q, tq, b);
    blend = b;
  }
  // 0x00475f20 would lift each foot to its ski here; its switches
  // [0x006084f0] / [0x006084f4] are never set, so it does nothing.
  legs[0]->reach(lp, lq);
  legs[1]->reach(rp, rq);
  for (int side = 0; side < 2; ++side) {
    Arm* arm = arms[side];
    if (arm->destroyed) continue;
    float q[4];
    if (!a.arm(side, nullptr, q)) continue;
    arm->set_pose(nullptr, q);
    if (arm->hand && a.hand(side, nullptr, q)) arm->hand->set_pose(nullptr, q);
  }
  if (detail & 2) pose_toes(a, blend);
  hip->set_pose(&hp, hq);
  torso->set_pose(nullptr, tq);
}

// Mech3.exe 1.2 @ 0x004177e7..0x004179fd (JumpJets): the jump pose, or the
// stand without one, as it is (not advanced): legs, toes, hip, torso.
void Mech::pose_jump() {
  if (!anims[12] && !anims[2]) return;  // ours: no pose to take (a mech without its motions)
  MechAnim& a = anims[12] ? *anims[12] : *anims[2];
  Vec3 hp{0, 0, 0}, lp, rp;
  float hq[4], lq[4], rq[4], tq[4];
  a.hip(&hp.x, hq);
  hp.z = 0.0f;
  a.foot(0, &lp.x, lq);
  a.foot(1, &rp.x, rq);
  a.torso_rotation(tq);
  legs[0]->reach(lp, lq);
  legs[1]->reach(rp, rq);
  if (detail & 2) pose_toes(a, 1.0f);
  hip->set_pose(&hp, hq);
  torso->set_pose(nullptr, tq);
}

// Mech3.exe 1.2 @ 0x00416fb8..0x00417293 (ActionUpdate): every part placed
// straight from the one-shot's keys (no IK), hip at (0, root y, 0).
void Mech::pose_action(MechAction& a, const Vec3& hp, const float hq[4]) {
  Vec3 lp, rp, tp;
  float lq[4], rq[4], tq[4];
  a.foot(0, &lp.x, lq);
  a.foot(1, &rp.x, rq);
  a.torso(&tp.x, tq);
  hip->place(&hp, hq);
  legs[0]->foot->place(&lp, lq);
  legs[1]->foot->place(&rp, rq);
  torso->place(nullptr, tq);
  a.thigh(0, &lp.x, lq);
  a.thigh(1, &rp.x, rq);
  legs[0]->place(nullptr, lq);
  legs[1]->place(nullptr, rq);
  a.calf(0, &lp.x, lq);
  a.calf(1, &rp.x, rq);
  legs[0]->calf->place(nullptr, lq);
  legs[1]->calf->place(nullptr, rq);
  if (a.arm(0, nullptr, lq)) arms[0]->place(nullptr, lq);
  if (a.arm(1, nullptr, rq)) arms[1]->place(nullptr, rq);
  if (arms[0]->hand && a.hand(0, nullptr, lq)) arms[0]->hand->place(nullptr, lq);
  if (arms[1]->hand && a.hand(1, nullptr, rq)) arms[1]->hand->place(nullptr, rq);
  pose_toes(a, 1.0f);
}

}  // namespace mech3
