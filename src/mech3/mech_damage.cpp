// The mech's side of damage: a hit arriving, knockback, area damage, death,
// limping and falls (docs/damage.md). Original file: mech3\mech.cpp (these
// sit between it and mech_anim.cpp, SetMaxSpeed before it); split from
// mech.cpp (heat) for size.
#include <cmath>
#include <cstring>

#include "crt/msvc_rand.h"
#include "mech3/mech3_interp.h"
#include "mech3/weapon.h"
#include "mech3/mech.h"
#include "mech3/mech_data.h"
#include "mech3/mech_names.h"
#include "gamez/zclass/class.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_control.h"
#include "mech3/mech_parts.h"

namespace mech3 {

namespace {
constexpr int kDead = 10, kFallenBack = 12, kFallenFront = 13;
constexpr int kElemental = 7;  // roster index
constexpr int kNetDeath = 0x13;
constexpr int kMessageNuke = 0x305;    // Mech3Msg.dll: the NUKE's name

DamageCalls& calls() { return damage_calls(); }
void* screen(Mech& m) { return heat_calls().screen ? heat_calls().screen(m) : nullptr; }

// Mech3.exe 1.2 @ 0x0051e210: the horizontal direction of v (y left as is).
void flat_normal(const Vec3& v, Vec3& out) {
  const float len = std::sqrt((v.x * v.x + 0.0f * 0.0f) + v.z * v.z);
  const float f = (!(len < 0.0) && !(len > 0.0)) ? len : 1.0f / len;
  out.x = f * v.x;
  out.z = v.z * f;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0041ac60 (Mech vtable +0x164): a weapon hit arrives from
// zWeapon (docs/damage.md, the hit path).
int Mech::take_damage(WeaponDef* def, Collision* coll, float damage) {
  int result = 0;
  if (remote) return 0;  // a network mech's vt+0x164 is 0x0046ef50: its damage comes with its states
  if (anim_state == kDead || !active) return 0;
  DamageCalls& c = calls();
  World& w = world();
  attacker = c.attacker ? c.attacker() : nullptr;
  if (c.controls_hit) c.controls_hit(*this, def, coll, damage, attacker);
  else if (controls) controls->on_hit(attacker);  // IControls vt+0x94
  WeaponType* type = def ? def->type : nullptr;
  float target_heat = 0.0f;
  Vec3 impulse{0, 0, 0};
  Vec3* pushed = nullptr;
  // Mech vtable +0x5c (0x0040bae0) is 0 for every mech.
  if (type) {
    target_heat = type->heat_2;
    if (def && (def->flags & 2)) target_heat = target_heat * w.dt;
    if (type->is_narc) {
      mark_narc(true);
      return 0;
    }
  }
  WeaponType* t = def ? def->type : nullptr;
  const float force = invincible ? 0.0f : t->impact_force;
  if (force > 0.0 && anim_state >= 1 && anim_state <= 9) {
    // Knockback along the shot, flat; the stagger check is per frame (0x417a80).
    Vec3 dir{w.shot[0].x - w.shot[1].x, w.shot[0].y - w.shot[1].y, w.shot[0].z - w.shot[1].z};
    dir.y = 0.0f;
    flat_normal(dir, impulse);
    impulse = {force * impulse.x, force * impulse.y, force * impulse.z};
    pushed = &impulse;
    knock_vec = {knock_vec.x + impulse.x, knock_vec.y + impulse.y, knock_vec.z + impulse.z};
    knock = std::sqrt((knock_vec.x * knock_vec.x + knock_vec.y * knock_vec.y) + knock_vec.z * knock_vec.z);
    if (this == w.mission_player && w.force_feedback && type && c.ff_hit) c.ff_hit(type->id, &impulse, force);
  }
  if (coll->x20 && c.roster_x1c) {
    if (Handle x1c = c.roster_x1c(*this); x1c && decals && c.damage_decal) c.damage_decal(x1c, decals, coll, damage);
  }
  if (this == w.player) screen(*this);  // result unused in the exe
  MechPart* part = c.node_part ? c.node_part(coll->node) : nullptr;
  const char* nuke = c.message ? c.message(kMessageNuke) : "";
  killed_by_nuke = std::strcmp(def->desc, nuke) == 0;
  if (part) {
    result = part->take_hit(def, coll, damage, pushed);
  } else {
    // A hit on the mech but on no part: spread over the body, never the head.
    if (!(c.node_owner && c.node_owner(coll->node)) || attacker == static_cast<Handle>(this)) {
      note_hit();
      return result;
    }
    float d = damage * 0.143f * interp_globals().damage_scale;  // 0x005bce4c, 0x005e1f68
    if (this == w.mission_player) d = w.difficulty * d;
    torso->apply_damage(d, 0);
    torso->apply_damage(d, 1);
    torso->apply_damage(d, 2);
    torso->apply_damage(0.2f * d, 3);  // 0x005e2004
    torso->apply_damage(0.2f * d, 4);
    torso->apply_damage(0.2f * d, 5);
    legs[0]->apply_damage(d, 0);
    legs[1]->apply_damage(d, 0);
    arms[0]->apply_damage(d, 0);
    arms[1]->apply_damage(d, 0);
  }
  add_heat(target_heat);
  note_hit();
  return result;
}

// Mech3.exe 1.2 @ 0x0041ac10. ECM makes a mech immune to NARC.
void Mech::mark_narc(bool on) {
  if (equipment[8] > 0) narc_time = 0.0f;
  else narc_time = on ? 30.0f : 0.0f;
}

// Mech3.exe 1.2 @ 0x0041d170
void Mech::note_hit() {
  last_hit_time = world().time;
  last_hit_part = torso;
  if (calls().last_hit) calls().last_hit(*this);  // test hook
  else aim_at_last_hit();
}

// Mech3.exe 1.2 @ 0x0041b420 (vtable +0x80, from the victim's Die): a mech
// killed scores 300, 400, 500 or 700 by its weight class (100 for an
// Elemental killer), a vehicle 75, a building 200. In the campaign the
// player's or a lancemate's kill goes to the mission's salvage.
void Mech::on_kill(Handle victim) {
  DamageCalls& c = calls();
  auto view = [&](Handle v, int slot) -> Handle {
    if (c.victim_view) return c.victim_view(v, slot);
    // A mech's 0x004d2a70 (itself) and 0x0040bae0 (0): killer_notify gives only mechs.
    return slot == 0x48 ? nullptr : v;
  };
  if (!victim) return;
  const Handle info = view(victim, 0x2c);
  if (!info) return;
  bool credit = false;
  if (heat_calls().game_type && heat_calls().game_type() == 1) {
    Mech* player = world().mission_player;
    credit = this == player || (player->team & allies) != 0;  // its vt+0x28 is itself
  }
  const Handle mech = view(info, 0x50);
  const Handle vehicle = view(info, 0x40);
  const Handle building = view(info, 0x48);
  if (c.controls_killed) c.controls_killed(*this, info);
  else if (controls) controls->on_kill(info);  // IControls vt+0xb4
  if (mech) {
    xf8 = -1;  // 0x0041d810(-1): no part targeted (0x0041d6f0 then does nothing)
    if (credit && c.salvage_mech) c.salvage_mech(mech);
    ++x3f4[0];
    int32_t points = 0;
    if (pilot->roster == kElemental) {
      points = 10;
    } else {
      static const int32_t kPoints[4] = {30, 40, 50, 70};
      const uint32_t k = static_cast<uint32_t>(weight_class(static_cast<Mech*>(mech)->pilot->roster));
      if (k <= 3) points = kPoints[k];
    }
    x3f4[3] += static_cast<uint32_t>(points * 10);  // fild, fmul 10.0 (0x005bcd54), ftol
  } else if (vehicle) {
    if (credit && c.salvage_vehicle) c.salvage_vehicle(vehicle);
    ++x3f4[1];
    x3f4[4] += 75;
  } else if (building) {
    ++x3f4[2];
    x3f4[5] += 200;
  }
}

// Mech3.exe 1.2 @ 0x0041d130. note_hit always names the torso, so after the
// first hit (and a second into the mission, hit_timeout) others aim at the
// torso's node instead of the marker 0.65 up (docs/damage.md).
void Mech::aim_at_last_hit() { set_target_node(last_hit_part ? last_hit_part->node : xe8); }

// Mech3.exe 1.2 @ 0x0041d070, as the torso's destruction starts the fall:
// the pilot ejects (the canned "eject" effect at the hip), the hip sinks,
// the eject sound plays. A mech lying down does neither.
void Mech::death_prep() {
  x634 &= ~1u;  // 0x0041ca60(0)
  if (anim_state == kFallenBack || anim_state == kFallenFront) return;
  Vec3 at{};
  if (part_calls().world_position) part_calls().world_position(hip->node, &at);  // 0x004ed530
  else zcls::node_world_position(static_cast<zcls::Node*>(hip->node), &at.x);
  const Handle eject = calls().canned ? calls().canned(*this, 0, nullptr, &at.x, nullptr)
                                     : canned_effect(*pilot, 0, nullptr, &at.x, nullptr);
  if (eject) {
    if (collide_calls().crouch) collide_calls().crouch(*hip, 0.75f);
    else hip->duck(0.75f);
    // Stopped again by Die at once, with every mech sound (0x0041b0d0).
    if (detail & 1) sounds[10] = heat_calls().play_sound ? heat_calls().play_sound(*this, 10) : nullptr;
  }
  if (this == world().mission_player && calls().mission_player_died) calls().mission_player_died();  // 0x0048f9e0
}

// Mech3.exe 1.2 @ 0x0041b0d0 (Mech vtable +0x70)
void Mech::die() {
  if (anim_state == kDead) return;
  DamageCalls& c = calls();
  World& w = world();
  if (attacker && c.killer_notify) c.killer_notify(attacker, *this);
  if (this == w.mission_player) {
    if (w.force_feedback && c.ff) {
      c.ff(0x433530, 0);
      c.ff(0x433900, 1);
      c.ff(0x4338d0, 1);
    }
    if (c.networked && c.networked() && c.net_event) c.net_event(*this, kNetDeath, reinterpret_cast<intptr_t>(attacker));
  }
  prev_state = anim_state;
  anim_state = kDead;
  damaged = true;
  vec_58 = {0, 0, 0};
  velocity = vec_58;
  vec_3c = vec_58;
  if (c.weapons_stop) c.weapons_stop(*this);
  if (this == w.player && c.view_mode && c.view_mode() == 1 && c.view_camera) c.view_camera(2);
  for (Sound& s : sounds) {
    if (s) {
      if (heat_calls().stop_sound) heat_calls().stop_sound(s);
      s = nullptr;
    }
  }
  if (c.controls_stop) c.controls_stop(*this);
  else if (controls) controls->on_death();  // IControls vt+0xa8
  foot(0)->stop_effect();
  foot(1)->stop_effect();
  if (void* scr = this == w.player ? screen(*this) : nullptr; scr && c.player_died_hud) c.player_died_hud(scr);
  if (!actions.empty()) {
    const int k = actions.front();
    if ((k == 6 || k == 5) && c.stop_action) c.stop_action(action_objects[k]);
  }
  if (this == w.mission_player) {
    if (c.mission_player_died) c.mission_player_died();
    if (killed_by_nuke) {
      // The display shows a nuked player with no armor left anywhere.
      legs[0]->set_armor(0.0f, 0);
      legs[1]->set_armor(0.0f, 0);
      arms[0]->set_armor(0.0f, 0);
      arms[1]->set_armor(0.0f, 0);
      head->set_armor(0.0f, 0);
      for (int s = 0; s < 6; ++s) torso->set_armor(0.0f, s);
    }
  }
}

// Mech3.exe 1.2 @ 0x0041ba40 (vtable +0x98; the AI's gates read it, ai.md
// 17.3): 0 dead, 1 invincible, else each part's lost internal weighed (head
// 1, legs 0.5, arms 0.2, the centre torso 1, its sides 0.5) and multiplied,
// in float a step at a time as at the game's 24-bit x87.
float Mech::health() {
  if (!damaged) return x40c;
  float h;
  if (anim_state == 10) {
    h = 0.0f;
  } else if (invincible) {
    h = 1.0f;
  } else {
    struct Weighed { MechPart* part; int32_t i; float w; };
    const Weighed ws[8] = {{head, 0, 1.0f}, {legs[0], 0, 0.5f}, {legs[1], 0, 0.5f}, {arms[0], 0, 0.2f},
                           {arms[1], 0, 0.2f}, {torso, 0, 1.0f}, {torso, 1, 0.5f}, {torso, 2, 0.5f}};
    h = 1.0f;
    for (const Weighed& x : ws) {
      const float lost = 1.0f - x.part->get_internal(x.i);
      const float wl = x.w * lost;
      h = (1.0f - wl) * h;
    }
  }
  x40c = h;
  damaged = false;
  return x40c;
}

// Mech3.exe 1.2 @ 0x0041b5f0. On the Elemental any part lost is death.
void Mech::on_part_destroyed(MechPart* part) {
  if (!part) return;
  if (calls().weapons_lost) calls().weapons_lost(*this, part->id);
  if (pilot->roster == 7) torso->destroy(1, 1);
}

// Mech3.exe 1.2 @ 0x0041cfa0 (mission scripts, heat)
void Mech::destroy_part(int id) {
  MechPart* p = nullptr;
  switch (id) {
    case kTorso: p = torso; break;
    case kLeftArm: p = arms[0]; break;
    case kRightArm: p = arms[1]; break;
    case kLeftThigh: p = legs[0]; break;
    case kRightThigh: p = legs[1]; break;
    case kHead: p = head; break;
    case kHip: p = hip; break;
    default: return;
  }
  p->destroy(0, 1);
}

// Mech3.exe 1.2 @ 0x0041d5f0. Returns whether the mech limps now.
bool Mech::start_limp(int leg, bool falls) {
  if (limp != 0 && limp != 1 && !invincible) {
    if (falls) {
      const float throttle = heat_calls().throttle ? heat_calls().throttle(*this) : 0.0f;
      knocked_down(throttle > 0.0f ? 3 : 4);
    }
    MechAnim* anim = nullptr;
    if (leg == 0 && anims[0]) {
      limp = 0;
      anim = anims[0];
    } else if (leg == 1 && anims[1]) {
      limp = 1;
      anim = anims[1];
    }
    if (anim) anims[11] = calls().blend_anim ? static_cast<MechAnim*>(calls().blend_anim(anims[4], anim)) : nullptr;
  }
  return limp == 0 || limp == 1;
}

// Mech3.exe 1.2 @ 0x0041b640: forwards or backwards by the speed.
void Mech::fall() { knocked_down(vec_3c.z > 0.0f ? 3 : 4); }

// Mech3.exe 1.2 @ 0x0041b660. 3 falls forward, 4 backward, when the mech
// has the animations; the knockback built up is cleared either way.
bool Mech::knocked_down(int kind) {
  DamageCalls& c = calls();
  bool down = false;
  if (anim_state != 12 && anim_state != 13 && anim_state != kDead && anim_state != 11) {
    auto ok = [&](int k) { return c.anim_ok ? c.anim_ok(*this, k, 0) : queue_action(k, false); };
    if (kind == 3) down = ok(1) && ok(3);
    else if (kind == 4) down = ok(2) && ok(4);
    else down = static_cast<uint8_t>(kind) != 0;
  }
  if (down) {
    static const Vec3 zero{0, 0, 0};  // 0x0063d870
    if (c.hip_reset) c.hip_reset(*hip, &zero);
    else hip->set_crouch(&zero);
    if (this == world().mission_player && world().force_feedback && c.ff) c.ff(0x433840, 0);
  }
  knock = 0.0f;
  knock_vec = {0, 0, 0};
  return down;
}

// Mech3.exe 1.2 @ 0x004143c0. The gaits are rescaled; walking is 60%.
void Mech::set_max_speed(float v) {
  DamageCalls& c = calls();
  for (int i = 0; i < 13; ++i) {
    if (!anims[i] || !(i < 0 || (i > 1 && i != 11))) continue;
    if (c.anim_speed) c.anim_speed(anims[i], v);
    else anims[i]->set_stride_speed(v);
  }
  if (c.gait_speed) c.gait_speed(*this, v);
  else if (pilot) scale_gaits(*pilot, gait, v);
  max_speed = v;
  walk_speed = v * 0.6f;
}

// Mech3.exe 1.2 @ 0x00417a10. Only the dead thigh critical calls it; it
// rebuilds the knockdown threshold from 5 and 80 where the constructor used
// 0.376 x 10 and 40 (docs/damage.md).
void Mech::scale_knockdown(float f) {
  const float a = f * knockdown[0];
  const float b = a * 0.8f;
  const float big = b * 80.0f;
  const float small = b * 5.0f;
  knockdown[0] = a;
  knockdown[1] = b;
  knockdown[2] = big;
  knockdown[3] = small;
  knockdown[4] = big - small;
}

// Mech3.exe 1.2 @ 0x00416770
float Mech::tonnage() const { return max_mass(pilot->roster); }

}  // namespace mech3
