#include "mech3/turret.h"

#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "mech3/veh_controls.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmath/zmth_matrix.h"
#include "mech3/weapon.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3::tur {

namespace {
TurretHooks g_default_hooks;
TurretHooks* g_hooks = &g_default_hooks;

Handle g_markers[3] = {};      // [0x00609a38..0x00609a40]
zrdr::Node* g_root = nullptr;  // [0x00609a44] aiTurrets.zrd
int32_t g_count = 0;           // [0x00609a48]
int32_t g_turn = 0;            // [0x00609a4c] the pick's turn
std::vector<Turret*> g_list;   // ours: zEntity's list holds them in the exe

// A list's count (its first slot) and item i's value as the exe reads it,
// whatever its type. FIX (bug): a node that is not a list, or an item past
// its end, reads as none (the exe reads past the list; no shipped file has
// one; docs/fixes.md).
int32_t count(const zrdr::Node* n) { return n && n->type == zrdr::List ? n->list[0].i : 0; }
const zrdr::Node* item(const zrdr::Node* n, int32_t i) { return i < count(n) ? &n->list[i] : nullptr; }
const char* str(const zrdr::Node* n, int32_t i) {
  const zrdr::Node* v = item(n, i);
  return v && v->type == zrdr::String ? v->s : nullptr;
}
float raw_float(const zrdr::Node* n, int32_t i) {  // a float's bits, or an int's
  const zrdr::Node* v = item(n, i);
  return v ? zgame::from_bits(static_cast<uint32_t>(v->i)) : 0.0f;
}

double dbits(uint64_t u) {
  double d;
  std::memcpy(&d, &u, 8);
  return d;
}
const double kPi = dbits(0x400921fb54442eeaull);   // [0x005c82e0] 3.14159265359
const double kTwoPi = dbits(0x401921fb54442eeaull);  // [0x005c82e8]
const double kDegToRad = dbits(0x3f91df46a2529983ull);  // [0x005c8860] 0.01745329251994
const float kPitch = zgame::from_bits(0x3fc90fd8u);             // 1.570796, the push in 0x004a4ac0's callers
const float kHalfPi = zgame::from_bits(0x3fc90fdbu);
const float kTwoPiF = zgame::from_bits(0x40c90fdbu);

// The square root as the aim (0x004a71c0) and 0x004abea0 take it: the
// float's bits halved, plus 0x1fc00000 (half the exponent's bias).
float fake_sqrt(float x) {
  return zgame::from_bits(static_cast<uint32_t>((static_cast<int32_t>(zgame::bits(x)) >> 1) + 0x1fc00000));
}

// The x87's "not equal to 0": unordered counts as equal.
bool nonzero(float x) { return x < 0.0f || x > 0.0f; }

float fsin(float a) { return static_cast<float>(std::sin(static_cast<double>(a))); }
float fcos(float a) { return static_cast<float>(std::cos(static_cast<double>(a))); }
}  // namespace

void set_turret_hooks(TurretHooks* h) { g_hooks = h ? h : &g_default_hooks; }
TurretHooks& turret_hooks() { return *g_hooks; }
int32_t& turret_count() { return g_count; }
int32_t& turret_turn() { return g_turn; }

zrdr::Node* TurretHooks::read(const char* file) { return zrdr::load(file); }
void TurretHooks::free_tree(zrdr::Node* root) { zrdr::free_node(root); }
void TurretHooks::node_world_position(Handle node, float out[3]) {
  unported("0x004ed650", {node});
  out[0] = out[1] = out[2] = 0;
}
void TurretHooks::node_rotation(Handle node, float out[3]) {
  unported("0x004fbe90", {node});
  out[0] = out[1] = out[2] = 0;
}
void TurretHooks::node_translation(Handle node, float out[3]) {
  unported("0x004fc020", {node});
  out[0] = out[1] = out[2] = 0;
}
int32_t TurretHooks::node_box(Handle node, float box[6]) {
  (void)box;
  return static_cast<int32_t>(unported("0x004ebd50", {node}));
}

// --- The turn limits (0x004a3fc0..0x004a4b30) ----------------------------------------

// Mech3.exe 1.2 @ 0x004a4a70 over 0x004a4000 (0x004a3fc0 twice).
void TurnLimits::make_full(float lo, float hi) {
  lo_sin = fsin(lo), lo_cos = fcos(lo);
  hi_sin = fsin(hi), hi_cos = fcos(hi);
  kind = LimitKind::Full;
}

// Mech3.exe 1.2 @ 0x004a4b30: the old one's destructor (vt+0, not freed:
// it lives in the turret), then the kind by the yaw's range, compared in
// the x87 against a float 0.001 and the double pi and 2 pi.
void TurnLimits::make(float lo, float hi, float ylo, float yhi) {
  const float range = yhi - ylo;
  if (!(range > zgame::from_bits(0x3a83126fu))) {  // 0x004a42f0
    lo_sin = fsin(lo), lo_cos = fcos(lo), hi_sin = fsin(hi), hi_cos = fcos(hi);
    lo_edge[0] = fcos(ylo), lo_edge[1] = 0.0f, lo_edge[2] = -fsin(ylo);
    kind = LimitKind::Fixed;
    return;
  }
  if (static_cast<double>(range) >= kTwoPi) {
    make_full(lo, hi);
    return;
  }
  // 0x004a4580 / 0x004a4800 over 0x004a4270.
  lo_sin = fsin(lo), lo_cos = fcos(lo), hi_sin = fsin(hi), hi_cos = fcos(hi);
  lo_edge[0] = -fcos(ylo), lo_edge[1] = 0.0f, lo_edge[2] = fsin(ylo);
  hi_edge[0] = fcos(yhi), hi_edge[1] = 0.0f, hi_edge[2] = -fsin(yhi);
  kind = static_cast<double>(range) >= kPi ? LimitKind::Wide : LimitKind::Narrow;
}

// Mech3.exe 1.2 @ 0x004a4180 / 0x004a4330 / 0x004a45b0 / 0x004a4830 (vt+0x08).
// Unordered compares pass the pitch and fail the edges.
bool TurnLimits::within(const float d[3]) const {
  if (lo_sin > d[1] || d[1] > hi_sin) return false;
  switch (kind) {
    case LimitKind::Fixed: {
      const float across = d[0] * d[0] + d[2] * d[2];
      const float off = d[0] * lo_edge[0] + d[2] * lo_edge[2];
      return across * zgame::from_bits(0x3727c5acu) > off * off;  // [0x005c82d0] 1e-5
    }
    case LimitKind::Narrow:
    case LimitKind::Wide: {
      const float a = d[0] * lo_edge[0] + d[2] * lo_edge[2];
      const float b = d[0] * hi_edge[0] + d[2] * hi_edge[2];
      const bool in_a = a >= 0.0f, in_b = b >= 0.0f;
      return kind == LimitKind::Narrow ? in_a && in_b : in_a || in_b;
    }
    case LimitKind::Full:
      break;
  }
  return true;
}

// --- The turret ----------------------------------------------------------------------

// Mech3.exe 1.2 @ 0x004a5c60, over AiTarget 0x004d56e0(0, 0) (zEntity3d
// 0x005a7e10(0), team 0). The team is the enemies' (0x004d5af0(1): 2); the
// limits start a full turn on -pi/2..pi/2 (0x004a4af0), then -1.570796..
// 1.570796 (0x004a4ac0).
Turret::Turret(Handle s, Handle n, Handle generic_anim, zrdr::Node* def) : self(s) {
  TurretHooks& h = *g_hooks;
  h.entity_init(self);
  set_team(0);
  limits.make_full(-kHalfPi, kHalfPi);  // 0x004a4af0: 0x004a4a70(-pi/2, pi/2, -pi, pi)
  set_team(2);
  id = g_count++;
  limits.make(-kPitch, kPitch, -kTwoPiF, kTwoPiF);  // 0x004a4ac0
  if (n) {
    set_node(n);
    h.entity_list(self);
  }
  setup(generic_anim, def);
}

// Mech3.exe 1.2 @ 0x004a6230 (with 0, 0, 0): a turret losing its node
// takes the weapons' handlers off it first.
void Turret::set_node(Handle n) {
  TurretHooks& h = *g_hooks;
  if (!n && node) h.remove_handlers(node);
  h.entity_set_node(self, n, &target_node, &centroid_node);  // 0x004d6c90: unbinds the old node first
  node = n;  // (0x005a8390's, inside it)
}

// Mech3.exe 1.2 @ 0x004a6270: the definition read (docs/vehicles.md, The
// turrets' definitions).
void Turret::setup(Handle generic_anim, zrdr::Node* def) {
  TurretHooks& h = *g_hooks;
  Handle destroy = generic_anim;
  if (const Handle a = h.anim_find(h.node_name(node))) destroy = a;  // an animation named as the node
  if (const zrdr::Node* parts = zrdr::find(def, "PARTS")) {
    healthy = h.node_child(node, "healthy");
    if (healthy) {
      active = 1;
      if (count(parts) == 5) {
        firepoints = 2;
        turret = h.node_find(node, str(parts, 1));
        gun = h.node_find(node, str(parts, 2));
        fire_node[0] = h.node_find(node, str(parts, 3));
        fire_node[1] = h.node_find(node, str(parts, 4));
      }
      if (count(parts) == 4) {
        firepoints = 1;
        turret = h.node_find(node, str(parts, 1));
        gun = h.node_find(node, str(parts, 2));
        fire_node[0] = h.node_find(node, str(parts, 3));
      } else if (count(parts) == 3) {
        firepoints = 1;
        gun = h.node_find(node, str(parts, 1));
        fire_node[0] = h.node_find(node, str(parts, 2));
      }
    }
  }
  if (const zrdr::Node* d = zrdr::find(def, "DEACTIVATE")) {
    off_node = h.node_named(str(d, 1));
    for (int32_t i = 2; i < count(d); ++i) off_node = h.node_child(off_node, str(d, i));
  }
  if (const zrdr::Node* e = zrdr::find(def, "EFFECT")) {
    effect = h.node_find(node, str(e, 1));
    effect_time = raw_float(e, 2);
    if (effect) h.model_scroll(h.node_model(effect));
  }
  per_damage = 1.0f;
  health = 1.0f;
  zrdr::get_float(def, "HEALTH", &per_damage);
  per_damage = 1.0f / per_damage;
  if (const char* s = zrdr::get_string(def, "DESTROY_ANIM")) destroy = h.anim_find(s);
  auto anims = [&](const char* key, std::list<Handle>& out, bool claim) {
    const zrdr::Node* l = zrdr::find(def, key);
    if (!l) return;
    for (int32_t i = 0; i < count(l) - 1; ++i) {
      const Handle a = h.anim_find(str(l, i + 1));
      if (!a) continue;
      if (claim) h.anim_claim(self, a);
      out.push_back(a);
    }
  };
  anims("ONFIRE_ANIM", onfire, false);
  anims("WAKEUP_ANIM", wakeup, false);
  anims("SLEEP_ANIM", sleep, false);
  anims("AUX_ANIM", aux, true);
  bool b;
  if (zrdr::get_bool(def, "IS_IDLE", &b)) idle = b ? 1 : 0;
  if (const zrdr::Node* snd = zrdr::find(def, "SOUNDS"))
    if (const zrdr::Node* fire = zrdr::find(const_cast<zrdr::Node*>(snd), "FIRE")) fire_sound = h.sound_find(str(fire, 1));
  int32_t box_hit = 1;
  if (zrdr::Node* lim = zrdr::find(def, "LIMITS")) {
    float yaw[2] = {-360.0f, 360.0f};
    if (zrdr::get_vec2(lim, "YAW", yaw)) {
      yaw_lo = static_cast<float>(static_cast<double>(yaw[0]) * kDegToRad);
      yaw_hi = static_cast<float>(static_cast<double>(yaw[1]) * kDegToRad);
      limits.make(-kPitch, kPitch, yaw_lo, yaw_hi);
    }
  }
  zrdr::get_int(def, "BBOX", &box_hit);
  if (zrdr::Node* w = zrdr::find(def, "WEAPON")) {
    const zrdr::Node* name = zrdr::find(w, "NAME");
    if (name && count(name) == 2) weapon = h.weapon_def(str(name, 1));
    zrdr::get_int(w, "AMMO", &ammo);
    // FIX (bug): a WEAPON without its weapon (tur_ams's NAME is empty) and
    // without DETECTION_RANGE reads the null definition's range in the
    // exe; the range stays 200 (no shipped turret has both; docs/fixes.md).
    if (!zrdr::get_float(w, "DETECTION_RANGE", &detect_range) && weapon) detect_range = h.weapon_range(weapon);
    if (const zrdr::Node* fl = zrdr::find(w, "FIRE_LIMITS")) {
      burst[0] = raw_float(fl, 1);
      burst[1] = raw_float(fl, 2);
    }
    zrdr::get_float(w, "FIRE_RATE", &rate);
  }
  float ams_range = 0.0f;
  if (zrdr::get_float(def, "AMS_RANGE", &ams_range)) {
    ams_on = 1;
    ams = new Ams(ams_range, 0.0f, 100);  // 0x004b3ab0
  }
  h.node_world_position(node, pos);
  h.node_rotation(node, facing);
  vec_normalise(facing);
  aim_from[0] = pos[0], aim_from[1] = pos[1], aim_from[2] = pos[2];
  if (turret) {
    turret_matrix = h.node_matrix(turret);
    aim_from[1] = h.matrix_y(turret_matrix) + aim_from[1];
  }
  if (gun) {
    gun_matrix = h.node_matrix(gun);
    aim_from[1] = h.matrix_y(gun_matrix) + aim_from[1];
  }
  // The firepoints' mean height (0 / 0 without one: the inactive turrets').
  float sum = 0.0f;
  int32_t n = 0;
  for (int k = 0; k < 2; ++k) {
    if (!fire_node[k]) continue;
    h.node_translation(fire_node[k], muzzle[k]);
    sum = sum + muzzle[k][1];
    ++n;
  }
  aim_from[1] = sum / static_cast<float>(n) + aim_from[1];
  if (effect) h.node_set_active(effect, 0);
  h.node_set_box_hit(healthy, box_hit);
  if (weapon && (h.weapon_flags(weapon) & 2) != 0) {
    beam = h.beam_make(weapon, node, gun, fire_from, fire_dir);
    rate = burst[0];
  }
  if (destroy) {
    destroy_anim = destroy;
    h.anim_reset(destroy, node);
    if (const Handle d = h.node_find(node, "destroyed")) h.node_set_active(d, 0);
    if (health > 0.0f && !h.set_hit_callback(self, node))
      h.report(0x221, "Failed to install weapons callback for (%s)", h.node_name(node));
  } else {
    h.report(0x226, "No destroy animation for (%s)", h.node_name(node));
  }
  h.nodes_pending();
  // The box's height and width (8 and 4 without one) place the markers and
  // give the AiTarget its height, unless TURRET_HEIGHT does.
  float tall = 8.0f, wide = 4.0f;
  float box[6];
  if (h.node_box(node, box) == 0) {
    tall = box[4] - box[1];
    wide = box[3] - box[0];
  }
  h.node_set_translation(centroid_node, 0.0f, tall * 0.5f, 0.0f);
  h.entity_markers(self);
  h.node_set_translation(target_node, 0.0f, tall * zgame::from_bits(0x3f266666u), 0.0f);
  h.entity_markers(self);
  if (zrdr::get_float(def, "TURRET_HEIGHT", &tall)) height = tall;
  else height = (tall > wide ? tall : wide) * zgame::from_bits(0x3f933333u);
  set_range(detect_range);
  h.entity_refresh(self);
}

// Mech3.exe 1.2 @ 0x004a5ff0 (0x004a5fd0 with the delete), over AiTarget's
// 0x004d58a0. The limits live in the turret.
Turret::~Turret() {
  TurretHooks& h = *g_hooks;
  delete ams;  // 0x005ac5c0, empty
  ams = nullptr;
  if (beam) beam = h.beam_free(beam);
  if (node) {
    set_node(0);
    h.entity_unlist(self);
  }
  clear_contacts();  // 0x004d58a0: 0x004a7be0, the node already gone
  net = nullptr;
  release_shadow();
  h.entity_end(self);
}

// Mech3.exe 1.2 @ 0x004a7af0
void Turret::aux_reset() {
  for (Handle a : aux) g_hooks->anim_reset(a, node);
}

// Mech3.exe 1.2 @ 0x004a74f0: 0x32 and 0x33 take IS_IDLE back and set it
// (the mission scripts' turret messages), 0x34 shuts it down destroyed.
void Turret::message(uint32_t from, int32_t msg) {
  if (msg == 0x32) {
    idle = 0;
  } else if (msg == 0x33) {
    idle = 1;
  } else if (msg == 0x34) {
    deactivate(true);
  } else {
    g_hooks->target_message(self, from, msg);
  }
}

// Mech3.exe 1.2 @ 0x004a7530 (through the callback 0x004a7940): the hit's
// damage over the definition's HEALTH; the attacker kept for the kill.
uint32_t Turret::hit(Handle weapon_def, Handle x, float damage) {
  (void)weapon_def, (void)x;
  TurretHooks& h = *g_hooks;
  health = health - per_damage * damage;
  attacker = h.node_object(h.hit_attacker());
  if (!(health > 0.0f)) {
    deactivate(true);
    h.hit_killed();
    return 0;
  }
  h.hit_health(health);
  return 0;
}

// Mech3.exe 1.2 @ 0x004a7600 (vt+0x70): killed outright, the player credited.
void Turret::kill() {
  health = 0.0f;
  attacker = g_hooks->player_vehicle();
  deactivate(true);
}

// Mech3.exe 1.2 @ 0x004a7620.
void Turret::deactivate(bool destroyed) {
  TurretHooks& h = *g_hooks;
  if (attacker) h.credit_kill(attacker, self);
  if (destroyed && destroy_anim && node) h.anim_run(destroy_anim, node);
  active = 0;
  // FIX (bug), behind bugs: the exe puts the effect out only when it is not
  // showing, so a charge lit as the turret dies stays lit (docs/fixes.md).
  if (effect) {
    const bool showing = (h.node_flags(effect) & 4) != 0;
    if (zfix::options().bugs ? showing : !showing) h.node_set_active(effect, 0);
  }
  h.remove_handlers(node);
  if (beam_on) {
    beam_on = 0;
    h.beam_stop(beam);
  }
  attacker = 0;
}

// Mech3.exe 1.2 @ 0x004a75b0 (vt+0x68): the target view's red tint, by the damage.
void Turret::tint(bool on) {
  TurretHooks& h = *g_hooks;
  if (!active) return;
  if (on) h.node_tint(node, (1.0f - health) * zgame::from_bits(0x3f19999au));
  h.node_tinted(node, on ? 1 : 0);
}

// Mech3.exe 1.2 @ 0x004a7a70
bool Turret::awake() const {
  TurretHooks& h = *g_hooks;
  if (!active || idle == 1) return false;
  return h.zones_meet(h.player_vehicle(), *this) == 1;
}

// Mech3.exe 1.2 @ 0x004a7ab0
bool Turret::in_range() const { return active && player_contact && detect_range * detect_range > player_contact->dist2; }

// --- The frame (step 4c2) -------------------------------------------------------------

uint32_t TurretHooks::frame_number() { return zcls::frames().frame; }
float TurretHooks::clock() { return weapon_world().clock; }
float TurretHooks::frame_dt() { return weapon_world().dt; }
void TurretHooks::node_world_cached(Handle node, float out[3]) {
  unported("0x004ed530", {node});
  out[0] = out[1] = out[2] = 0;
}
uint32_t TurretHooks::player_team(Handle player) {
  const Handle v = unported("obj.vt+0x24", {player});
  return target_team(unported("pv.vt+0x28", {v}));
}
bool TurretHooks::ams_find(Ams& a, int32_t side, const float at[3]) {
  return a.find(side, reinterpret_cast<const zwep::Vec3f3*>(at));
}
Handle TurretHooks::ams_target(Ams& a) {
  (void)a;
  return unported("0x004b3ba0", {});
}
void TurretHooks::ams_fire(Ams& a) { a.fire(1); }

// Mech3.exe 1.2 @ 0x004a6be0. Each step of the shot goes through the
// turret's turn to pick (0x004a70d0): on the other frames it neither aims
// nor fires (docs/vehicles.md, The turrets' frame).
void Turret::frame() {
  TurretHooks& h = *g_hooks;
  if (!(health > 0.0f)) {
    deactivate(true);
    return;
  }
  if (idle) return;
  h.node_world_cached(node, pos);
  const Handle player = h.player_vehicle();
  // FIX (bug): a turret without a healthy node (no PARTS) reads its flags
  // through null in the exe; it is shut down instead (docs/fixes.md).
  if (!healthy || (h.node_flags(healthy) & 4) == 0 || (h.node_flags(node) & 4) == 0 ||
      (off_node && (h.node_flags(off_node) & 4) == 0)) {
    deactivate(false);
    return;
  }
  const float now = h.clock();
  if (weapon && (h.weapon_flags(weapon) & 0x4000) != 0) {  // a mine: it goes off at a target
    if (pick(player)) {
      h.anim_run(destroy_anim, node);
      h.detonate(weapon, pos);
    }
    return;
  }
  if (effect && (h.node_flags(effect) & 4) != 0 && !(now >= next_fire)) h.effect_animate(h.node_model(effect));
  if (!pick(player)) {
    aimed = 0;
  } else if (aim(target_at) && aimed && now >= next_fire) {
    if (effect && (h.node_flags(effect) & 4) == 0) {
      // The effect first: lit for EFFECT's time, the shot when it is over.
      next_fire = effect_time + now;
      h.node_set_active(effect, 1);
    } else {
      if (nonzero(burst[0]) && !nonzero(burst_end)) burst_end = now + burst[0];
      if (effect) h.node_set_active(effect, 0);
      if (firepoints > 1) {
        if (++firepoint >= firepoints) firepoint = 0;
      }
      const float* m = muzzle[firepoints > 1 ? firepoint : 0];
      fire_from[0] = m[0], fire_from[1] = m[1], fire_from[2] = m[2];
      h.matrix_push();
      h.matrix_identity();
      h.node_to_world(gun);
      h.point_through(fire_from);
      float t[3];
      h.read_vec(target_at, t);
      fire_dir[0] = t[0] - fire_from[0];
      fire_dir[1] = t[1] - fire_from[1];
      fire_dir[2] = t[2] - fire_from[2];
      vec_normalise(fire_dir);
      h.matrix_pop();
      if (weapon && nonzero(h.weapon_lead(weapon))) {
        const float slope = h.aim_slope(weapon, fire_from, target_at);
        if (slope < -1.0f || slope > -1.0f) lead_dir(fire_dir, slope);
      }
      if (ams_on && ams_found) {
        h.ams_fire(*ams);
        ams_found = 0;
      }
      if (fire_sound) h.sound_play(fire_sound, pos);
      if (beam) {
        if (!beam_on) {
          beam_on = 1;
          h.shooter_set(*this, true);
          h.beam_start(beam);
          h.shooter_set(*this, false);
        }
      } else {
        if (weapon) {
          if ((h.weapon_fire_flags(weapon) >> 1) & 1) h.attach_fire(weapon, fire_node[firepoint]);
          h.shooter_set(*this, true);
          // 0x004b4a40's last: the shot hostile to the player (the player's
          // team among the turret's enemies, as a mech's shots are marked).
          const int32_t hostile = (enemies & h.player_team(player)) != 0 ? 1 : 0;
          h.weapon_fire(weapon, node, fire_from, fire_dir, x178, hostile);
          h.shooter_set(*this, false);
        }
        for (Handle a : onfire) h.anim_run(a, node);
      }
      // A burst (FIRE_LIMITS) ends in its pause, else the next shot is FIRE_RATE away.
      if (nonzero(burst[0]) && now >= burst_end) {
        burst_end = 0.0f;
        aimed = 0;
        next_fire = burst[1] + now;
      } else {
        next_fire = rate + now;
      }
    }
  }
  if (beam && !aimed && beam_on) {
    beam_on = 0;
    h.beam_stop(beam);
  }
}

// FIX (B), behind per_second: the turn by game time, a 30 fps frame each:
// due when a 1/30 s step since the last ask, counted from 0, leaves `id`
// over `n`, as the exe's frame count does at 30 fps (docs/fixes.md). One
// turn at most per ask.
bool turn_due(int64_t& seen, float clock, uint32_t n, int32_t id) {
  if (!n) return false;
  const int64_t now = static_cast<int64_t>(std::floor(static_cast<double>(clock) * zfix::kReferenceFps));
  if (seen < 0 || seen > now) seen = now - 1;
  const int64_t from = seen + 1;
  seen = now;
  if (from > now) return false;
  // The first step at or after `from` that leaves `id`.
  const int64_t k = static_cast<int64_t>(n);
  const int64_t first = from + ((id - from % k) % k + k) % k;
  return first <= now;
}

// Mech3.exe 1.2 @ 0x004a70d0. Without an AMS a turret looks for a target
// only on the frames whose number, over the turrets ever made
// ([0x00609a48], never reset), leaves its id; then the next turn's count
// [0x00609a4c] moves on and, unless it wrapped to 0, the nearest enemy in
// sight becomes the target (0x004d72b0). An AMS turret takes the nearest
// shot on the radar every frame. Either way the target ends as the player's
// contact (0x004a7b40) while the shot goes at what was picked.
bool Turret::pick(Handle player) {
  TurretHooks& h = *g_hooks;
  Handle at = 0;
  if (!ams_on) {
    const uint32_t n = static_cast<uint32_t>(g_count);  // at least this turret
    const bool due = zfix::options().per_second ? turn_due(turn_seen, h.clock(), n, id)
                                                : n && h.frame_number() % n == static_cast<uint32_t>(id);
    if (due) {
      g_turn = static_cast<int32_t>((static_cast<uint32_t>(g_turn) + 1) % n);
      if (g_turn != 0) pick_enemy();
      const Handle e = target ? target->seen : 0;
      if (e) at = h.entity_position(e);
    }
  } else {
    // Side 0: the shots not hostile to the player (a mech's AMS takes it as
    // the player's enemy, 0x0041927e); the turrets are all the enemy's.
    ams_found = h.ams_find(*ams, 0, pos) ? 1 : 0;
    if (ams_found) at = h.ams_target(*ams);
  }
  if (!at) {
    picked = 0;
    return false;
  }
  target_player(player);
  shooter = player;
  picked = 1;
  x1a0 = 1;
  target_at = at;
  x1ac = 0;
  return true;
}

// Mech3.exe 1.2 @ 0x004d72b0: contacts nearest first.
void Turret::pick_enemy() {
  TurretHooks& h = *g_hooks;
  for (ai::Contact* c : contacts) {
    const Handle v = h.contact_vehicle(c);
    if ((h.target_team(h.vehicle_target(v)) & enemies) == 0) continue;
    if (!h.line_clear(self, v)) continue;
    h.set_target(*this, c);
    return;
  }
}

// Mech3.exe 1.2 @ 0x004a7b40 / 0x004a7ba0.
bool Turret::target_player(Handle player) {
  TurretHooks& h = *g_hooks;
  if (target && target->seen == player) return true;
  for (ai::Contact* c : contacts) {
    if (c->seen == player) {
      h.set_target(*this, c);
      return true;
    }
  }
  h.set_target(*this, nullptr);
  return false;
}

// Mech3.exe 1.2 @ 0x004a71c0: the facing (+0x124, in the node's frame)
// eased toward the target (e^-(3 dt) of the old kept a frame, 8 dt with an
// AMS), unless the limits refuse it; then the turret's matrix (its yaw) and
// the gun's (its pitch) set from it. Aimed: inside the detection range and
// within about 27 degrees (cos 0.89) of the target.
bool Turret::aim(Handle at) {
  TurretHooks& h = *g_hooks;
  // FIX (bug): without a gun (PARTS of one name, or its node not found) the
  // exe reads the gun's matrix through null; such a turret cannot aim.
  if (!gun_matrix) return false;
  float v[3] = {h.matrix_at(gun_matrix, 9), h.matrix_at(gun_matrix, 10), h.matrix_at(gun_matrix, 11)};
  h.matrix_push();
  h.matrix_identity();
  h.node_to_world(node);
  h.point_through(v);
  float t[3];
  h.read_vec(at, t);
  float d[3] = {t[0] - v[0], t[1] - v[1], t[2] - v[2]};
  const float dist = vec_normalise(d);
  h.dir_through_inverse(d);
  h.matrix_pop();
  const float old[3] = {facing[0], facing[1], facing[2]};
  const float k = zmth::exp_neg_dt(h.frame_dt() * (ams_on ? 8.0f : 3.0f), h.frame_dt());
  const float k1 = 1.0f - k;
  facing[0] = k * facing[0] + k1 * d[0];
  facing[1] = k * facing[1] + k1 * d[1];
  facing[2] = k * facing[2] + k1 * d[2];
  vec_normalise(facing);
  bool ok = false;
  if (!(dist >= detect_range)) {
    const float dot = (d[0] * facing[0] + d[1] * facing[1]) + d[2] * facing[2];
    ok = static_cast<double>(dot) > 0.89;
  }
  aimed = ok ? 1 : 0;
  const float hx = facing[0], hy = facing[1], hz = facing[2];
  // The flat length by the bit trick (half the exponent): within about 6% of
  // the square root (docs/vehicles.md, The turrets' aim).
  const float flat2 = hx * hx + hz * hz;
  const float flat = fake_sqrt(flat2);
  float c = 1.0f, sn = 0.0f;
  // FIX (bug): a facing straight up or down has no flat part, but the bit
  // trick gives 8e-20 for 0, so the exe takes the heading branch with
  // c = -0, sn = 0: the turret's matrix loses its yaw axes and the turret
  // (and the gun under it) is drawn flat, invisible (W50, c1m2's
  // triturret02). Its heading is then the one it has with none (c 1, sn 0).
  if (zfix::options().bugs ? nonzero(flat2) : nonzero(flat)) {
    c = -(hz / flat);
    sn = -(hx / flat);
  }
  if (!limits.within(facing)) {
    facing[0] = old[0], facing[1] = old[1], facing[2] = old[2];
    aimed = 0;
    return false;
  }
  if (turret) {
    h.matrix_put(turret_matrix, 0, c);
    h.matrix_put(turret_matrix, 2, -sn);
    h.matrix_put(turret_matrix, 6, sn);
    h.matrix_put(turret_matrix, 8, c);
    h.node_set_matrix(turret, turret_matrix);
    h.matrix_put(gun_matrix, 4, flat);
    h.matrix_put(gun_matrix, 5, hy);
    h.matrix_put(gun_matrix, 7, -hy);
    h.matrix_put(gun_matrix, 8, flat);
  } else {
    h.matrix_put(gun_matrix, 0, c);
    h.matrix_put(gun_matrix, 2, -sn);
    h.matrix_put(gun_matrix, 3, sn * hy);
    h.matrix_put(gun_matrix, 4, flat);
    h.matrix_put(gun_matrix, 5, hy * c);
    h.matrix_put(gun_matrix, 6, flat * sn);
    h.matrix_put(gun_matrix, 7, -hy);
    h.matrix_put(gun_matrix, 8, flat * c);
  }
  h.node_set_matrix(gun, gun_matrix);
  return true;
}

// Mech3.exe 1.2 @ 0x004abea0
void lead_dir(float d[3], float slope) {
  const float flat2 = d[2] * d[2] + d[0] * d[0];
  if (!nonzero(flat2)) {
    if (!nonzero(slope)) {
      d[0] = 0.0f, d[1] = 0.0f, d[2] = 1.0f;  // [0x005eda80]
      return;
    }
    const float f = fake_sqrt((1.0f - slope * slope) * 0.5f);
    d[0] = f, d[1] = slope, d[2] = f;
    return;
  }
  const float f = fake_sqrt((1.0f - slope * slope) / flat2);
  d[1] = slope;
  d[0] = f * d[0];
  d[2] = f * d[2];
}

// --- The file ---------------------------------------------------------------------------

// Mech3.exe 1.2 @ 0x004a76b0. A turret is made on every node of list 6
// whose name starts with a TURRET entry's (several entries can take the
// same node) and whose `healthy` child is active; nothing keeps it but
// zEntity's list.
bool turrets_load(const char* file, Handle at) {
  TurretHooks& h = *g_hooks;
  static const char* const kNames[3] = {"hud_turret_ally_sm", "hud_turret_enemy_sm", "hud_turret_other_sm"};
  for (int i = 0; i < 3; ++i) g_markers[i] = h.marker_create(kNames[i]);
  // FIX (bug), behind bugs: the count of turrets made is never reset, so
  // each mission's turrets pick (and so aim and fire) once every so many
  // frames as turrets have been made since the game started; counted from
  // this mission's (docs/fixes.md).
  if (zfix::options().bugs) g_count = g_turn = 0;
  zrdr::Node* root = h.read(file);
  if (!root) {
    h.report(0x487, "Failed to read %s", file);
    return false;
  }
  g_root = root;
  Handle generic = 0;
  if (const zrdr::Node* g = zrdr::find(root, "GENERIC_DESTROY_ANIM")) generic = h.anim_find(str(g, 1));
  zrdr::Node* list = zrdr::find(root, "TURRET");
  if (!list) return true;
  for (int32_t i = 1; i < count(list); i += 2) {
    const char* name = str(list, i);
    zrdr::Node* def = name ? zrdr::find(list, name) : nullptr;
    if (!def) continue;
    h.nodes_first(name);
    for (Handle node = h.nodes_next(); node; node = h.nodes_next()) {
      const Handle healthy = h.node_find(node, "healthy");
      if (!healthy || (h.node_flags(healthy) & 4) == 0) continue;
      void* mem = ::operator new(sizeof(Turret));
      Turret* t = new (mem) Turret(h.turret_handle(mem), node, generic, def);
      g_list.push_back(t);
      h.entity_place(t->self, h.world_node(), at);  // vt+0x38
      t->aux_reset();
    }
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004a7970: the turrets themselves go with zEntity's list.
void turrets_end() {
  TurretHooks& h = *g_hooks;
  for (Handle& m : g_markers) {
    if (m) h.marker_delete(m);
    m = 0;
  }
  if (g_root) {
    h.free_tree(g_root);
    g_root = nullptr;
  }
}

std::vector<Turret*>& turret_list() { return g_list; }

void turrets_delete_all() {
  for (Turret* t : g_list) g_hooks->turret_free(t);
  g_list.clear();
}
Handle turret_marker(int32_t side) { return side >= 0 && side < 3 ? g_markers[side] : 0; }

}  // namespace mech3::tur
