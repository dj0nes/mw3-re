#include "mech3/mech_move.h"

#include <cmath>
#include <iterator>

#include "crt/msvc_rand.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_matl.h"
#include "mech3/mech_config.h"
#include "mech3/mech_data.h"
#include "mech3/mech_names.h"
#include "mech3/mission_world.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

int32_t mw2_jump_jets();  // options.h, 0x0040d1e0 (its Handle clashes with mech_parts.h's)

namespace {
MoveCalls g_calls;

constexpr float kHeatSlow = 0.6f;     // 0x005bcd50
constexpr float kWadeStart = 0.1f;    // 0x005bcde8, of the height
constexpr float kWade = 0.7f;         // 0x005e1fb4
constexpr float kSpeedRate = 0.6f;    // 0x005e1fb8
constexpr float kFootfall = 1.25f;    // 0x005e1fbc, frames of slack
constexpr float kStepSpeed = 0.4f;    // 0x005e1fc4
constexpr float kStepScale = 2.5f;    // 0x005e1fc8
constexpr float kTurnSlow = 0.8f;     // 0x005e1fcc
constexpr float kCreep = 0.99f;       // 0x005bce04, of walk.lo
constexpr float kAirTurn = 0.85f;     // the turn's throttle when airborne
constexpr float kJetClimb = 8.0f;     // 0x005e1f8c, per unit of thrust
constexpr float kJetPush = 5.0f;      // 0x005e1f90
constexpr float kJetPushShare = 0.7f; // 0x005bcd98
constexpr float kJetSteer = 5.5f;     // 0x005e1f94
constexpr float kJetStrafeMax = 40.0f;  // 0x005bcdd0
constexpr float kJetImpulseMax = 6.0f;  // 0x005e1f98
constexpr float kLandingScale = 80.0f;  // 0x005e1f84
constexpr float kLandingSideways = 20.0f;  // 0x005e1f88
constexpr int kStateStand = 1, kDead = 10, kFalling = 11, kFallenBack = 12, kFallenFront = 13;
constexpr int kNetAction = 0x17;  // docs/network-messages.md
constexpr int kKnockSound = 0x18;  // cockpit table 0x00605598
constexpr uint32_t kBlendFrames = 20;  // 0x005e1fac
constexpr int kMasc = 12, kMascSound = 0x1b;  // equipment kind; the cockpit sound (table 0x00605598)
constexpr int kJumpJet = 11, kElementalRoster = 7, kJetSound = 3, kSoundJumpJet = 3, kJumpPose = 12;
constexpr int kStandUpSound = 0xb, kStandUpSlot = 11;  // mech +0x5e4
constexpr float kDustAbove = -50.0f, kDustBelow = 50.0f;  // 0x005bcda0, 0x005bcda4

enum Slot { kStand = 2, kStep = 3, kWalk = 4, kTrot = 5, kRun = 6, kStepWalk = 8, kWalkTrot = 9,
            kTrotRun = 10, kLimpBlend = 11 };
enum Lo { kLo = 0, kHi = 1 };

void play_gait(Mech& m, float dt, MechAnim* a, float speed) {
  if (g_calls.move) g_calls.move(m, dt, a, speed);
  else m.move(dt, a, speed);
}

float frac(float x) {
  const double d = x;
  return static_cast<float>(d - std::floor(d));
}

// The cross product as 0x004190f3 evaluates it.
Vec3 cross(const Vec3& p, const Vec3& c) {
  return {p.y * c.z - p.z * c.y, p.z * c.x - p.x * c.z, p.x * c.y - p.y * c.x};
}
}  // namespace

MoveCalls& move_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x00417a80. A grounded local mech whose knock exceeds its
// threshold falls; then the animation moves it (root motion into +0x44),
// blended in over the first 20 frames of walking (docs/movement.md, Move).
void Mech::move(float dt, MechAnim* anim, float speed) {
  if (!no_power_down && !remote) {
    float t = tonnage();
    if (t < 10.0f) t = 10.0f;  // 0x005bcd54
    const float threshold = (t - 10.0f) * 0.0111111114f * knockdown[4] + knockdown[3];  // 0x005bcde4
    if (threshold <= knock) {
      zmth::Vec3 v{knock_vec.x, knock_vec.y, knock_vec.z};
      zmth::flat_normalize(v, v);
      knock_vec = {v.x, v.y, v.z};
      if (this == world().player && heat_calls().play_mech_sound) heat_calls().play_mech_sound(kKnockSound);
      stagger(knock_vec);
      knock = 0.0f;
      knock_vec = {0, 0, 0};
    }
  }
  const Vec3 saved = vec_3c;
  if (anim) {
    anim->set_speed(speed);
    anim->advance(dt);
    anim->velocity(&vec_3c);
  }
  const int jets = jump_jets(dt);
  ground_step(dt);
  if (!anim || !(detail & 0x10)) {
    foot_ground(*legs[0]->foot);
    foot_ground(*legs[1]->foot);
    return;
  }
  if (!g_calls.gait_pose) {
    pose_gait(*anim, jets, saved);  // 0x00417bbc..0x0041818d (mech_pose.cpp)
    return;
  }
  // The test hook: the blend-in of the root motion only, the pose logged.
  const float blend = gait_blend(jets);
  if (blend >= 0.0f)
    vec_3c = {(vec_3c.x - saved.x) * blend + saved.x, (vec_3c.y - saved.y) * blend + saved.y,
              (vec_3c.z - saved.z) * blend + saved.z};
  g_calls.gait_pose(*this, *anim, blend < 0.0f ? 1.0f : blend);
}

// Mech3.exe 1.2 @ 0x00417300 (one call per jump-jet item at setup): the
// burn rate is set here only, so losing jets later does not change it.
void Mech::add_jump_jet(bool add) {
  int32_t& jets = equipment[kJumpJet];
  if (add) ++jets;
  else if (jets > 0) --jets;
  const int32_t limit = config->jump_jet_limit();
  if (jets > limit) jets = limit;
  else if (jets < 0) jets = 0;
  if (jets > 0) {
    const float t = pilot->roster == kElementalRoster ? 45.0f : tonnage();  // 0x005bcdb8
    jet_burn = t / static_cast<float>(jets * 100);
  } else {
    jet_burn = 0.0f;
  }
}

// Mech3.exe 1.2 @ 0x0041e9c0
float Mech::altitude() const {
  const Vec3* p = skis_calls().position ? skis_calls().position(const_cast<Mech&>(*this)) : nullptr;
  return (p ? p->y : 0.0f) - skis->ground[6];
}

// Mech3.exe 1.2 @ 0x004173b0. Thrust goes to the skis as a climb impulse;
// the player's jets push along the throttle unless the MW2 option is on
// (docs/movement-jump.md). The pose of a firing jump is pose_jump.
int Mech::jump_jets(float dt) {
  if (!(equipment[kJumpJet] > 0)) {  // +0x580 is left as it was
    if (sounds[kJetSound]) {
      if (heat_calls().stop_sound) heat_calls().stop_sound(sounds[kJetSound]);
      sounds[kJetSound] = nullptr;
    }
    if (jet_effect) {
      if (g_calls.stop_effect) g_calls.stop_effect(jet_effect);
      jet_effect = nullptr;
    }
    return 0;
  }
  const bool assist = this == world().mission_player && mw2_jump_jets() == 0;
  const float thrust = controls->jump;  // GetJumpThrust
  int firing = 0;
  const double need = jump_jets_firing == 0 && !no_power_down ? 0.25 : 0.0;  // 0x005bcdc0
  if (jet_fuel > need) {
    if (thrust > 0.0) {
      firing = 1;
      impulse.y = (thrust * kJetClimb + environment().gravity) * dt;
    }
    if (assist) {
      if (firing && height * 0.5f < altitude()) {
        const float dir = controls->throttle < 0.0f ? -1.0f : 1.0f;
        vec_3c.z = vec_3c.z - thrust * kJetPush * dt * dir * kJetPushShare;
      }
    } else if (no_power_down) {
      if (controls->jj_forward != 0.0f) {
        firing = 1;
        vec_3c.z = vec_3c.z - kJetSteer * dt * controls->jj_forward;
      }
      if (controls->jj_strafe != 0.0f) {
        firing = 1;
        vec_3c.x = kJetSteer * dt * controls->jj_strafe + vec_3c.x;
      }
      if (vec_3c.z > max_speed) vec_3c.z = max_speed;
      else if (vec_3c.z < -max_speed) vec_3c.z = -max_speed;
      if (vec_3c.x > kJetStrafeMax) vec_3c.x = kJetStrafeMax;
      else if (vec_3c.x < -kJetStrafeMax) vec_3c.x = -kJetStrafeMax;
    }
  }
  // A cap of 6 a frame. FIX (B): 6 per 1/30 s.
  const float cap = kJetImpulseMax * zfix::frame_steps(dt);
  if (impulse.y > cap) impulse.y = cap;
  else if (impulse.y < 0.0f) impulse.y = 0.0f;
  if (firing) jet_fuel = jet_fuel - jet_burn * dt;
  else if (!airborne() && jet_fuel < 1.0f) jet_fuel = jet_fuel - jet_burn * dt * -0.5f;  // 0x005bcdd8
  if (jet_fuel > 1.0f) jet_fuel = 1.0f;
  else if (jet_fuel < 0.0f) jet_fuel = 0.0f;
  if (infinite_fuel) jet_fuel = 1.0f;

  if (detail & 1) {
    Sound& s = sounds[kJetSound];
    if (firing && !s) s = g_calls.pilot_sound ? g_calls.pilot_sound(*this, kSoundJumpJet, 0.3f, 1.0f) : nullptr;
    else if (!firing && s) s = g_calls.fade_sound ? g_calls.fade_sound(s) : nullptr;
  }
  const bool ff = this == world().mission_player && world().force_feedback;
  if (firing) {
    if (!jet_effect && jet_node) {
      jet_effect = g_calls.pilot_effect ? g_calls.pilot_effect(*this, 1, jet_node) : nullptr;
      if (ff && g_calls.ff_jets) g_calls.ff_jets(1);
    }
  } else if (jet_effect) {
    if (g_calls.stop_effect) g_calls.stop_effect(jet_effect);
    jet_effect = nullptr;
    if (ff && g_calls.ff_jets) g_calls.ff_jets(0);
  }
  if (!firing && jump_jets_firing && anims[kJumpPose]) anims[kJumpPose]->seek(-1);
  jump_jets_firing = firing;
  if (firing) {
    if (g_calls.jet_pose) g_calls.jet_pose(*this);
    else pose_jump();  // 0x004177e7..0x004179fd (mech_pose.cpp)
  }
  return jump_jets_firing;
}

// Mech3.exe 1.2 @ 0x004162c0. A local mech that comes down hard, or fast
// sideways, can fall (20 % or 40 %); a hard landing hurts the legs.
void Mech::ground_step(float dt) {
  const float vy0 = vec_3c.y;
  const bool was = no_power_down;
  no_power_down = g_calls.skis_update ? g_calls.skis_update(*this, dt) : skis->update(dt);
  if (remote || !was || no_power_down || !(vy0 < 0.0f) || pilot->roster == kElementalRoster) return;
  float jolt = -(vy0 / kLandingScale);
  bool hard = false;
  if (jolt > 1.0f) jolt = 1.0f, hard = true;
  else if (jolt < 0.0f) jolt = 0.0f, hard = true;
  if (this == world().mission_player && world().force_feedback && g_calls.ff_landing) g_calls.ff_landing(jolt);
  const bool sideways = kLandingSideways < std::fabs(vec_3c.x);
  if (sideways) hard = true;
  const float thr = -vec_3c.z / max_speed;  // vtable +0x11c
  controls->throttle = thr;
  if (thr > 0.0f) {
    prev_state = anim_state;
    anim_state = 5;
  }
  if (hard) {
    const float keep = sideways ? 0.6f : 0.8f;
    if (static_cast<float>(crt::msvc_rand()) * 3.051851e-05f > keep) {  // 0x005bcd94
      knocked_down(vec_3c.z > 0.0f ? 4 : 3);
      return;
    }
  }
  if (jolt > 0.7f) {  // 0x005bcd98
    const float d = jolt * 0.005f;  // 0x005bcd9c
    legs[0]->apply_damage(d, 0);
    legs[1]->apply_damage(d, 0);
  }
  if (detail & 8) {
    controls->set_throttle(0.0f);
    if (g_calls.landing_dust) g_calls.landing_dust(*this);
    else ground_dust();
    if (g_calls.landing_pose) g_calls.landing_pose(*this);
    else landing_pose();
    momentum = vec_3c;
  }
}

// Mech3.exe 1.2 @ 0x0041df50. On: the top speed x1.25 with the throttle
// rescaled so the speed holds (vt +0x114 throttle x max, +0x11c speed /
// max); off: the engine's top speed and no rescale, so the mech slows at once.
void Mech::masc(bool on) {
  if (!(equipment[kMasc] > 0) || on == (masc_time != 0.0f)) return;
  const float base = engine_speed(config->engine, max_mass(config->mech));
  if (on) {
    if (this == world().mission_player && heat_calls().play_mech_sound) heat_calls().play_mech_sound(kMascSound);
    const float speed = controls->throttle * max_speed;
    set_max_speed(base * 1.25f);  // 0x005bce70
    controls->throttle = speed / max_speed;
    masc_time = 0.001f;
  } else {
    set_max_speed(base);
    masc_time = 0.0f;
  }
}

// Mech3.exe 1.2 @ 0x00462dc0
void Foot::set_pivot(float rate) {
  pivot = true;
  pivot_rate = rate;
}

// Mech3.exe 1.2 @ 0x00418e00: the leg to pivot on, the planted one or the lower.
Leg* Mech::pivot_leg() const {
  if (airborne()) return nullptr;
  const int s0 = legs[0]->foot->ground_state, s1 = legs[1]->foot->ground_state;
  if (s0 == 3 && s1 != 3) return legs[0];
  if (s0 != 3 && s1 == 3) return legs[1];
  return legs[1]->foot->clearance < legs[0]->foot->clearance ? legs[1] : legs[0];
}

// Mech3.exe 1.2 @ 0x00418fc0 (vtable +0x170). Turning in place forces the
// throttle up to a creep (docs/movement.md, Turning).
void Mech::turn(float) {
  MechControls& c = *controls;
  if (c.turn == 0.0f) return;
  float a = kAirTurn;
  if (!airborne()) {
    a = std::fabs(c.throttle);
    if (gait[kWalk][kLo] * kCreep > a) {
      const float creep = gait[kWalk][kLo] * kCreep;
      c.throttle = c.reverse ? -creep : creep;
    }
  }
  const float rate = (1.0f - std::fabs(a) * kTurnSlow) * turn_rate;
  const float w = c.turn * rate;
  const Vec3 av{0.0f, w, 0.0f};
  if (detail & 4) {
    if (Leg* leg = pivot_leg()) {
      if (const Vec3* p = leg->foot->translation) {
        const Vec3 d = cross(*p, av);
        vec_3c = {vec_3c.x + d.x, vec_3c.y + d.y, vec_3c.z + d.z};
        leg->foot->set_pivot(-w);
      }
    }
  }
  vec_58 = av;
}

// Mech3.exe 1.2 @ 0x004181c0. Picks the gait animation from the throttle and
// plays it through 0x00417a80 (docs/movement.md, Speed and gait).
void Mech::select_gait(float dt) {
  MechControls& c = *controls;
  float thr = c.throttle;
  float spd = std::fabs(thr);
  const float target = c.throttle_target;
  const int st = anim_state;
  const bool asks_walk = !(st == 1 || st == 2 || st == 3) && gait[kStep][kHi] <= std::fabs(target);
  blend_weight = 0.0f;
  if (st == 12 || st == 13) return;
  if (powered_down) return;
  const bool planted = foot(0)->ground_state == 3 && foot(1)->ground_state == 3;
  const bool turning = c.turn != 0.0f || torso->centring == 2;
  if (airborne()) {
    play_gait(*this, dt, nullptr, thr);
    return;
  }

  speed_target = 1.0f;
  if (heat > kHeatSlow) {
    float x = kHeatSlow / heat;
    x = x > 1.0f ? 1.0f : x < kHeatSlow ? kHeatSlow : x;
    speed_target = x * speed_target;
  }
  const float d0 = foot(0)->water_depth, d1 = foot(1)->water_depth;
  const float d = d0 < d1 ? d0 : d1;
  const float h = height;
  const bool ff = this == world().mission_player && world().force_feedback;
  if (d > h * kWadeStart) {
    if (zfix::options().bugs) {
      // FIX (bug): from full speed at a tenth of the height down to 70% at
      // full depth, where the exe fell to a standstill and then jumped back
      // to 70% (movement.md, Speed and gait; fixes.md).
      float frac = (d - h * kWadeStart) / (h - h * kWadeStart);
      frac = frac > 1.0f ? 1.0f : frac;
      speed_target = (1.0f - (1.0f - kWade) * frac) * speed_target;
    } else {
      speed_target = d < h ? (h - d) * kWade / h * speed_target : kWade * speed_target;
    }
    if (ff && !wading) {
      if (g_calls.wading_effect) g_calls.wading_effect(1);
      wading = true;
    }
  } else if (ff && wading) {
    if (g_calls.wading_effect) g_calls.wading_effect(0);
    wading = false;
  }
  if (limp == 0 || limp == 1) speed_target = speed_target * kHeatSlow;
  if (thr < 0.0f) speed_target = speed_target * kHeatSlow;
  speed_factor = (speed_target - speed_factor) * kSpeedRate * dt + speed_factor;
  thr = thr * speed_factor;
  spd = spd * speed_factor;
  if (crouched && spd > gait[kStep][kHi]) {
    if (g_calls.crouch) g_calls.crouch(*this, 0);
    else crouch(false);
  }

  // Leaving stand or step: hold just under walking until a foot comes down
  // in the step animation. Out of this block the step/walk seek index is 0
  // or 1 wherever it is used; the original passes the frame step's bits
  // otherwise, on a path that cannot reach the seek.
  float thr2 = thr;
  int footfall = 0;
  if (!asks_walk && anim_index <= kStep && !(spd < gait[kWalk][kLo])) {
    anims[kStepWalk]->copy_phase(*anims[anim_index]);
    anims[kWalk]->copy_phase(*anims[anim_index]);
    const MechAnim& s = *anims[kStep];
    const float fr = frac(s.t / s.loop_time);
    const float a = s.events[0] / s.loop_time, b = s.events[1] / s.loop_time;
    bool hit = false;
    if (thr > 0.0f) {
      if (fr > a && fr - frame_dt * kFootfall < a) hit = true, footfall = 0;
      else if (fr > b && fr - frame_dt * kFootfall < b) hit = true, footfall = 1;
    } else if (thr < 0.0f) {
      if (fr < a && fr + frame_dt * kFootfall > a) hit = true, footfall = 0;
      else if (fr < b && fr + frame_dt * kFootfall > b) hit = true, footfall = 1;
    }
    if (!hit) {
      const float lim = static_cast<float>(gait[kWalk][kLo] - 0.0001);  // 0x005bcdf0
      spd = lim;
      const float sign = thr < 0.0f ? -1.0f : 1.0f;
      const float five = 5.0f * lim, t2 = 5.0f * thr2;  // 0x005e1fc0
      thr = sign * lim;
      thr2 = t2 > five ? five : t2 < -five ? -five : t2;
    }
  }

  bool stop;
  if (turning) stop = false;
  else if (spd > gait[kStep][kLo] && (spd > gait[kWalk][kLo] || target != 0.0f)) stop = false;
  else if (asks_walk) stop = false;
  else {
    stop = true;
    if (anim_index == kStep && !planted) stop = frac(anims[kStep]->t / anims[kStep]->loop_time) < dt + dt;
  }

  auto enter = [&](int slot, int state) {
    anim_index = slot;
    prev_state = anim_state;
    anim_state = state;
  };
  auto follow = [&](int slot) {
    if (anim_index != slot) anims[slot]->copy_phase(*anims[anim_index]);
  };
  auto weight = [&](float lo, float hi) { return (spd - lo) / (hi - lo); };

  if (!(detail & 0x10)) {  // far away: always the walk
    enter(kWalk, 5);
    play_gait(*this, dt, anims[kWalk], thr);
    return;
  }
  if ((limp == 0 || limp == 1) && anims[kLimpBlend] && !stop && !(spd < gait[kWalk][kLo])) {
    prev_state = anim_state;
    anim_state = 0;
    anim_index = kStepWalk;  // not 11 (docs/movement.md)
    anims[kLimpBlend]->set_blend(0.5f);
    const float lim = gait[kTrot][kLo];
    if (thr > lim) thr = lim;
    else if (thr < -lim) thr = -lim;
    play_gait(*this, dt, anims[kLimpBlend], thr);
    return;
  }
  if (stop) {
    enter(kStand, 1);
    play_gait(*this, dt, anims[kStand], thr);
    gait_frames = 0, gait_steps = 0;
    return;
  }
  if (spd < gait[kWalk][kLo] && !asks_walk) {
    if (anim_state == 1) anims[kStep]->seek(2);
    else follow(kStep);
    enter(kStep, 3);
    play_gait(*this, dt, anims[kStep], (kStepSpeed + thr2) * kStepScale);
    gait_frames = 0, gait_steps = 0;
    return;
  }
  if (spd < gait[kStep][kHi] && !asks_walk) {
    if (anim_index == kStep) {
      anims[kStepWalk]->seek(footfall);
      gait_frames = 0, gait_steps = 0;
    } else {
      follow(kStepWalk);
    }
    blend_weight = weight(gait[kWalk][kLo], gait[kStep][kHi]);
    enter(kStepWalk, 4);
    anims[kStepWalk]->set_blend(blend_weight);
    play_gait(*this, dt, anims[kStepWalk], thr);
    return;
  }
  int slot;
  if (spd < gait[kTrot][kLo]) {
    follow(kWalk);  // a byte at 0x0060562c, never set, would restart it instead
    enter(slot = kWalk, 5);
  } else if (spd < gait[kWalk][kHi]) {
    follow(kWalkTrot);
    blend_weight = weight(gait[kTrot][kLo], gait[kWalk][kHi]);
    enter(slot = kWalkTrot, 6);
    anims[kWalkTrot]->set_blend(blend_weight);
  } else if (spd < gait[kRun][kLo]) {
    follow(kTrot);
    enter(slot = kTrot, 7);
  } else if (spd < gait[kTrot][kHi]) {
    follow(kTrotRun);
    blend_weight = weight(gait[kRun][kLo], gait[kTrot][kHi]);
    enter(slot = kTrotRun, 8);
    anims[kTrotRun]->set_blend(blend_weight);
  } else if (spd > gait[kRun][kLo]) {
    follow(kRun);
    enter(slot = kRun, 9);
  } else {
    return;
  }
  play_gait(*this, dt, anims[slot], thr);
  ++gait_frames;
  gait_steps = gait_steps + zfix::frame_steps(dt);
}


// Mech3.exe 1.2 @ 0x0041eca0
void Foot::show_shadow(bool on) {
  if (shadow && damage_calls().node_set_active) damage_calls().node_set_active(shadow, on);
}

bool Mech::vital_part_lost() const {
  return anim_state == kDead || head->destroyed || legs[0]->destroyed || legs[1]->destroyed || torso->destroyed;
}

// Mech3.exe 1.2 @ 0x00416780. Remote mechs take actions only from the
// network (force); the same kind twice in a row is dropped. FIX (A): a
// kind past the table is none (a peer's 0x17 names any, fixes.md).
bool Mech::queue_action(int kind, bool force) {
  if (kind <= 0 || kind >= static_cast<int>(std::size(action_objects)) || !action_objects[kind] || anim_state == kDead)
    return false;
  if (remote && !force) return false;
  if (!actions.empty() && actions.back() == kind) return false;
  actions.push_back(kind);
  if (damage_calls().net_event) damage_calls().net_event(*this, kNetAction, kind);
  return true;
}

// Mech3.exe 1.2 @ 0x00416660
int Mech::state_for_action(int kind) const {
  const bool vital = vital_part_lost();
  switch (kind) {
    case 1: case 2: return vital ? kDead : kFalling;
    case 3: return vital ? kDead : kFallenFront;
    case 4: return vital ? kDead : kFallenBack;
    case 7: case 8: return kDead;
    default: return anim_state;
  }
}

// Mech3.exe 1.2 @ 0x00416720: mech_fall on landing, mech_falling otherwise.
int Mech::sound_for_action(int kind) {
  if (kind == 3 || kind == 4) return 0x13;
  return kind >= 1 && kind <= 8 ? 0x12 : -1;
}

// Mech3.exe 1.2 @ 0x0041b760 (the torso's death)
bool Mech::death_fall(int kind) {
  if (anim_state == kDead || anim_state == kFallenBack || anim_state == kFallenFront || anim_state == kFalling)
    return false;
  if (!queue_action(kind, false)) return false;
  if (kind == 1) return queue_action(3, false);
  if (kind == 2) return queue_action(4, false);
  return true;
}

// Mech3.exe 1.2 @ 0x0041b7f0: a knock from ahead throws the mech backwards.
void Mech::stagger(const Vec3& knock) {
  const float d = knock.x * forward.x + knock.z * forward.z;
  knocked_down(d > 0.0f ? 4 : 3);
}

// Mech3.exe 1.2 @ 0x0041b840
void Mech::get_up() {
  if (anim_state != kFallenBack && anim_state != kFallenFront) return;
  if (!active || is_powered_down()) return;
  queue_action(anim_state == kFallenBack ? 6 : 5, false);
}

// Mech3.exe 1.2 @ 0x00416860. Plays the queue's front action; when it ends,
// its effect lands and the next one that fits starts (docs/movement-jump.md).
void Mech::update_action(float dt) {
  int kind = actions.front();
  MechAction* act = action_objects[kind];
  if (!act) return;
  if (act->done) {
    world().last_action = kind;
    switch (kind) {
      case 3: case 4:
        if (!remote) {
          float s = static_cast<float>((100.0 - tonnage()) * 0.0099999998);  // 0x005bcda8, 0x005bcdb0
          s = s > 1.0f ? 1.0f : s < 0.0f ? 0.0f : s;
          const float d = (1.0f - s) * 0.01f;  // 0x005e236c
          legs[0]->apply_damage(d, 0);
          legs[1]->apply_damage(d, 0);
          if (controls && g_calls.controls_landed) g_calls.controls_landed(*this);
          lie_time = 5.0f;
          vec_3c = vec_58 = velocity = impulse = push = Vec3{0, 0, 0};
        }
        break;
      case 5: case 6:
        if (anim_state != kDead) {
          prev_state = anim_state;
          anim_state = kStateStand;
        }
        knock = 0.0f;
        knock_vec = {0, 0, 0};
        if (g_calls.weapons_enable) g_calls.weapons_enable(*this, 1);
        hud_fall = this == world().mission_player ? 0 : 1;
        legs[0]->foot->show_shadow(true);
        legs[1]->foot->show_shadow(true);
        if (g_calls.restore_camera) g_calls.restore_camera(*this);
        if (controls) controls->zero();  // vt +0xb0, 0x00428990
        break;
      default:
        break;
    }
    act->reset();
    actions.pop_front();
    bool found = false;
    while (!actions.empty() && !found) {
      const int k = actions.front();
      const int st = anim_state;
      const bool vital = vital_part_lost();
      const int last = world().last_action;
      if (action_objects[k]) {
        switch (k) {
          case 1: case 2: case 7: case 8:
            found = st != kDead && st != kFalling && st != kFallenBack && st != kFallenFront;
            break;
          case 3: found = st == kFalling || last == 4; break;  // 4, where 1 would pair (harmless)
          case 4: found = st == kFalling || last == 2; break;
          case 5: case 6:
            found = !vital && (st == kFallenBack || st == kFallenFront) && last != 5 && last != 6;
            break;
          default: found = true; break;
        }
      }
      if (!found) actions.pop_front();
    }
    if (!found) return;
    kind = actions.front();
    act = action_objects[kind];
    act->reset();
  }
  if (!act->started()) {
    const int next = state_for_action(kind);
    prev_state = anim_state;
    anim_state = next;
    if (detail & 1) {
      const int id = sound_for_action(kind);
      sounds[id] = g_calls.pilot_sound ? g_calls.pilot_sound(*this, id, 1.0f, -1.0f) : nullptr;
    }
    controls->set_throttle(0.0f);
    switch (kind) {
      case 1: case 2: case 7: case 8:
        if (g_calls.fall_camera) g_calls.fall_camera(*this);
        legs[0]->foot->show_shadow(false);
        legs[1]->foot->show_shadow(false);
        if (g_calls.weapons_enable) g_calls.weapons_enable(*this, 0);
        hud_fall = 1;  // 0x0041ca30(1): 1 for every mech
        break;
      case 3: case 4:
        if (g_calls.action_dust) g_calls.action_dust(*this);
        else ground_dust();
        break;
      default:
        break;
    }
    act->reset();
    float q[4];
    act->hip(&action_root.x, q);
    momentum = vec_3c;
  }
  // x0.6 a frame (0x005bcd50). FIX (B): 0.6 per 1/30 s (fixes.md, Frame rate).
  const float carry = zfix::options().per_second ? std::pow(0.6f, zfix::frame_steps(dt)) : 0.6f;
  momentum = {momentum.x * carry, momentum.y * carry, momentum.z * carry};
  vec_3c.y = momentum.y;
  ground_step(dt);
  act->advance(dt);
  Vec3 root{};
  float q[4] = {1, 0, 0, 0};
  act->hip(&root.x, q);
  const Vec3 d{root.x - action_root.x, root.y - action_root.y, root.z - action_root.z};
  action_root = root;
  vec_3c.x = d.x / dt + momentum.x;
  vec_3c.z = d.z / dt + momentum.z;
  const Vec3 hip{0.0f, root.y, 0.0f};
  if (g_calls.action_pose) g_calls.action_pose(*this, *act, hip, q);
  else pose_action(*act, hip, q);  // 0x00416fb8..0x00417293 (mech_pose.cpp)
}

// Mech3.exe 1.2 @ 0x0041bda0. Down while slower than the step's top speed
// (the hip's crouch target the crouch depth below); else up, the player's
// mech playing the stand-up sound if it was down. The Elemental never crouches.
void Mech::crouch(bool on) {
  if (pilot->roster == kElementalRoster) return;
  bool was = false;
  if (on && gait[kStep][kHi] > std::fabs(controls->throttle)) {  // GetThrottle
    const Vec3 down{0.0f, -crouch_depth, 0.0f};
    hip->set_crouch(&down);
    crouched = true;
  } else {
    was = crouched;
    static const Vec3 zero{0, 0, 0};  // 0x0063d870
    hip->set_crouch(&zero);
    crouched = false;
  }
  if (was && this == world().player)
    sounds[kStandUpSlot] = g_calls.pilot_sound ? g_calls.pilot_sound(*this, kStandUpSound, 1.0f, -1.0f) : nullptr;
}

// Mech3.exe 1.2 @ 0x004164f3..0x0041660b (a landing in 0x004162c0) and
// 0x00416d3a..0x00416e91 (landf / landb starting in ActionUpdate, through
// 0x0041eb90 and 0x0041ecf0, the same code): the world's ground under the
// torso, the mech itself left out, from 50 m above to 50 m below; on the
// highest found, a splash at it on soil 1, else dust on the hip moving with
// the mech.
void Mech::ground_dust() {
  const PartCalls& pc = part_calls();
  Vec3 p{0, 0, 0};
  if (pc.world_position) pc.world_position(torso->node, &p);
  else zcls::node_world_position(static_cast<zcls::Node*>(torso->node), &p.x);
  int32_t saved = 1;
  auto flag8 = [&](int32_t* get, int set) {
    if (pc.flag8) return pc.flag8(node, get, set);
    auto* n = static_cast<zcls::Node*>(node);
    return get ? zcls::node_get_flag8(n, get) : zcls::node_set_flag8(n, set);
  };
  flag8(&saved, 0);
  flag8(nullptr, 0);
  GroundHits hits;
  hits.count = 0;
  if (pc.ground_at) pc.ground_at(p.x, p.y - kDustAbove, p.z, &hits);
  flag8(nullptr, saved);
  float top = p.y - kDustBelow;
  bool found = false;
  uint32_t soil = 0;
  for (int i = 0; i < hits.count; ++i) {
    const GroundHit& h = hits.hit[i];
    if (!(top < h.height)) continue;
    top = h.height;
    soil = !h.material ? 0 : pc.material_soil ? pc.material_soil(h.material)
                                              : static_cast<zmod::Material*>(h.material)->soil;
    found = true;
  }
  if (!found) return;
  Handle fx = soil == 1 ? pilot->landing_splash : pilot->landing_dust;
  if (!fx) return;
  DamageCalls& dc = damage_calls();
  if (soil == 1) {
    const float args[9] = {p.x, top, p.z, 0, 0, 0, 0, 0, 0};
    if (dc.spawn_effect) dc.spawn_effect(fx, nullptr, args);
  } else if (dc.spawn_effect_at) {
    dc.spawn_effect_at(fx, hip->node, velocity.x, velocity.y, velocity.z);
  }
}

// Mech3.exe 1.2 @ 0x0041660b..0x0041662d: landf made ready and its root's
// start kept, so the landing's action starts from it.
void Mech::landing_pose() {
  MechAction* a = action_objects[3];
  a->reset();
  float q[4];
  a->hip(&action_root.x, q);
}

}  // namespace mech3
