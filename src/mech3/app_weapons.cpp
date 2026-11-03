// The weapons' hooks wired to the program (docs/weapons.md, In the program):
// the weapon manager's calls into the mech (mech3/weapon), zWeapon's into
// the scene graph, object3d and cls_di.c's queries (gamez/zweapon), and the
// mechs' damage handler. Not original code. Visuals (lights, animations,
// craters, marks) and sounds stay unset: the shots fly and hit without them.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zeffect/zeff_init.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zmodel/gmod_hit.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/app_mech.h"
#include "mech3/app_sensors.h"
#include "mech3/app_view.h"
#include "mech3/player_view.h"
#include "mech3/veh_basic.h"
#include "mech3/cockpit_sound.h"
#include "mech3/mech.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_move.h"
#include "mech3/mission_world.h"
#include "mech3/mech_skis.h"
#include "mech3/weapon.h"

namespace mech3 {

int32_t heat();  // 0x0040cfc0, mech3/options.h (not includable with mech.h)

namespace {
zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(h); }
Mech* mech_of(Handle v) { return static_cast<Mech*>(v); }

// The query record (0x504 bytes, 32 slots) into zWeapon's list (31 slots).
void to_hits(const zcls::LineHits& in, zwep::HitList* out) {
  out->count = in.count;
  out->total = in.total;
  const int n = in.count < 31 ? in.count : 31;
  const int t = in.total < 31 ? in.total : 31;
  const int k = n > t ? n : t;
  for (int i = 0; i < k; ++i) std::memcpy(&out->rec[i], &in.hit[i], sizeof(zwep::HitRecord));
}
static_assert(sizeof(zwep::HitRecord) == sizeof(zcls::LineHit), "the 0x28-byte slot");

zcls::Node* qworld() { return world_hooks().world; }
uint32_t (*g_unported)(const char*) = nullptr;
void unported(const char* key) {
  if (g_unported) g_unported(key);
}
zeff::AnimDef* anim_of(zwep::Handle a) { return static_cast<zeff::AnimDef*>(a); }

// What the weapons start, for the tests (ours): "anim <name>", "sound <name>".
std::vector<std::string> g_fx;
void note(const char* kind, const char* name) {
  if (g_fx.size() < 4096) g_fx.push_back(std::string(kind) + " " + (name ? name : "?"));
}
void note_anim(zwep::Handle a) {
  if (a) note("anim", anim_of(a)->name());
}
void note_sound(zwep::Handle s) {
  const auto* o = static_cast<const zsnd::Object*>(s);
  if (!o) return;
  note("sound", o->kind == 0 ? static_cast<const zsnd::Sound*>(o)->name : static_cast<const zsnd::Group*>(o)->name);
}

int32_t segment_list(const zwep::Vec3f3* a, const zwep::Vec3f3* b, zwep::HitList* out) {
  static zcls::LineHits r;
  if (!qworld()) return out->count = 0, 1;
  const int32_t res = zcls::segment_list(qworld(), &a->x, &b->x, &r);
  to_hits(r, out);
  return res;
}

int32_t segment_nearest(const zwep::Vec3f3* a, const zwep::Vec3f3* b, zwep::HitList* out) {
  static zcls::LineHits r;
  if (!qworld()) return out->count = 0, 1;
  const int32_t res = zcls::segment_nearest(qworld(), &a->x, &b->x, &r);
  to_hits(r, out);
  return res;
}

// Mech3.exe 1.2 @ 0x004161a0: the mech's damage handler, vtable +0x164
// (Mech::TakeDamage) on the hit.
int32_t g_hits = 0;                  // the handler's calls, for the program's tests
zwep::Handle g_last_attacker = nullptr;  // zWeapon's shooter node at the last ([0x009a0edc])

uint32_t mech_damage(zwep::Handle object, zwep::WeaponDef* def, zwep::HitRecord* hit, float damage) {
  ++g_hits;
  g_last_attacker = zwep::attacker();
  Collision c;
  c.x20 = hit->material;
  c.node = hit->node;
  return static_cast<uint32_t>(static_cast<Mech*>(object)->take_damage(def, &c, damage));
}


uint32_t g_zone;  // the spawn's zone word: the entity's +0x64 record (count, ids)
}  // namespace

// Mech3.exe 1.2 @ 0x0040e9d0: a vehicle's node (+0x28 id 0xf000) its owner,
// a part's (0xff) the part's mech (0x00460000).
void* node_owner(void* n) {
  zcls::Node* z = zn(n);
  if (!z) return nullptr;
  const uint32_t id = z->get<uint32_t>(0x28);
  if (id & 0xf000) return z->owner;
  if (id & 0xff) return static_cast<MechPart*>(z->owner)->mech;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0040e920 (0x0040eee0's target): the game object a node
// belongs to, by its +0x28 bits: 16, 18, then a vehicle's or a part's
// (0x0040e9d0), 19, 17, 20, 21, 22 have their owner at +0x40.
void* node_entity(void* n) {
  zcls::Node* z = zn(n);
  if (!z) return nullptr;
  const uint32_t id = z->get<uint32_t>(0x28);
  auto bit = [&](int b) { return (id >> b & 1) != 0; };
  if (bit(16) || bit(18)) return z->owner;  // 0x0040e960 tests 18 first of its own
  if (void* o = node_owner(n)) return o;
  for (int b : {19, 17, 20, 21, 22})
    if (bit(b)) return z->owner;
  return nullptr;
}

std::vector<std::string>& weapon_fx_log() { return g_fx; }

void wire_weapons(uint32_t (*log)(const char* key)) {
  g_unported = log;
  g_fx.clear();
  WeaponCalls& c = weapon_calls();
  // The weapons' own sounds (U_LOOPED, U_RECHARGE_SND ..) through zSound; the
  // cockpit's (0x0041e770's table) and Betty's voice are not ported.
  c.play_loop = [](Handle s) -> Handle {                                                   // 0x0056e3d0 (1.0)
    note_sound(s);
    return s ? zsnd::play(static_cast<zsnd::Object*>(s), 1.0f) : nullptr;
  };
  c.play_at = [](Handle s, const Vec3f* p) -> Handle {                                     // 0x0056e7e0 (1.0, 0)
    note_sound(s);
    return s ? zsnd::play_at(static_cast<zsnd::Object*>(s), &p->x, 1.0f, nullptr) : nullptr;
  };
  c.stop_sound = [](Handle v) { if (v) zsnd::stop(static_cast<zsnd::VoiceBase*>(v)); };  // 0x0056e9f0
  // The radar list's object (0x004b4ab0): its action empties the shots' list each frame.
  c.object3d_create = []() -> Handle { return zcls::object3d_create(); };
  c.set_action_priority = [](Handle n, int32_t p) { zcls::node_set_priority(zn(n), p); };
  c.set_action = [](Handle n, void (*fn)()) {
    static void (*act)() = nullptr;  // the node action takes the node; the radar's takes nothing
    act = fn;
    zcls::node_set_action(zn(n), fn ? +[](zcls::Node*) { act(); } : nullptr);
  };
  c.node_destroy = [](Handle n) { zcls::node_delete(zn(n)); };
  c.mech_sound = [](int32_t i) { cockpit_sound_play(i, 1.0f); };  // 0x0041e770
  // 0x004e6dc0 (1.0, 0, 0, -1.0, 0): the weapon's name and "destroyed" voices,
  // each through 0x00571cc0 (no place, volume 1, no fade). Their names in
  // the message panel only with [0x0060b878], which nothing sets.
  c.destroyed_sounds = [](Handle name, Handle destroyed) {
    for (Handle h : {name, destroyed})
      if (h) zsnd::play_fading(static_cast<zsnd::Object*>(h), nullptr, 1.0f, nullptr, -1.0f, 0);
  };
  c.voice_destroyed = [](int32_t) { unported("0x004853d0"); };
  c.vehicle_entity = [](Handle v) { return v; };                                          // vt+0x2c: itself
  c.vehicle_velocity = [](Handle v) { return reinterpret_cast<const Vec3f*>(&mech_of(v)->velocity); };
  c.entity_node = [](Handle e) { return mech_of(e)->node; };                              // +0x04
  // &+0x64: the shooter's zone record, its count and up to three ids (a shot
  // queries in those zones). Copied whole: the count alone left a vehicle's
  // shots (its record set by its ground probe, 0x005a80d0) querying zone 0,
  // so they crossed nothing.
  c.entity_x64 = [](Handle e) -> Handle {
    std::memcpy(&g_zone, &mech_of(e)->x64, 4);
    return &g_zone;
  };
  c.target_key = [](Handle t) { return t; };  // vt+0x24: a mech, vehicle, turret or target answers itself
  // The lock (0x004b3330 -> 0x004b3370), playtest #25: the mech's target
  // selected (+0x6c, a contact), what it sees (+4, its object: vt+0x2c is
  // itself), that one's mech, its aim point and velocity as pointers.
  c.entity_target = [](Handle e) -> Handle { return mech_of(e)->target; };
  c.record_target = [](Handle r) { return hud_contact_vehicle(r); };
  c.target_mech = [](Handle t) { return hud_object_mech(t); };  // vt+0x2c, vt+0x50
  c.target_position = [](Handle t) { return reinterpret_cast<const zwep::Vec3f3*>(weapon_lock_point(t)); };
  c.target_velocity = [](Handle t) { return reinterpret_cast<const zwep::Vec3f3*>(weapon_lock_velocity(t)); };
  c.ecm_count = [](Handle m) { return mech_of(m)->equipment[8]; };  // +0x3cc
  c.fcs_count = [](Handle m) { return mech_of(m)->equipment[3]; };  // +0x3b8
  c.equipment_in_part = [](Handle m, int32_t kind, int32_t part) {   // 0x00460c10
    MechPart* p = part_by_id(*mech_of(m), part);
    return p && p->has_equipment(kind);
  };
  c.lock_notice = [](Handle m, Handle, WeaponDef* d) {  // 0x0041abd0: no sound without the def
    if (d) mech_lock_warning(m);
  };
  c.entity_enemies = [](Handle e) { return mech_of(e)->enemies; };                         // +0xc8
  c.player_team = [] { return mech3::world().mission_player ? mech3::world().mission_player->team : 0u; };
  c.part_by_id = [](Handle owner, int32_t id) -> Handle { return part_by_id(*mech_of(owner), id); };
  c.part_node = [](Handle part) { return static_cast<MechPart*>(part)->node; };          // +0x08
  c.part_mech = [](Handle part) -> Handle { return static_cast<MechPart*>(part)->mech; };
  c.node_id = [](Handle node) { return node ? zn(node)->get<int32_t>(0x28) : -1; };
  c.node_name = [](Handle node) { return zn(node)->name(); };
  c.config = [](Handle owner) { return mech_of(owner)->config; };                         // +0x3ec
  c.heat = [](Handle owner) { return mech_of(owner)->heat; };                             // vt+0x9c
  c.add_heat = [](Handle owner, float h) {  // 0x00419e40
    if (owner) mech_of(owner)->add_heat(h);
  };                // 0x00419e40
  // A projectile's recoil (0x00461670): the firing part's vt+0x24. An arm
  // kicks (0x00466610), a hand passes it to its arm (0x004673f0), any other
  // part has none (0x004a5bc0). Playtest #28.
  c.recoil = [](Handle part, float r) {
    auto* p = static_cast<MechPart*>(part);
    if (auto* a = dynamic_cast<Arm*>(p)) a->kick(r);
    else if (auto* h = dynamic_cast<Hand*>(p)) {
      if (Arm* a = h->arm()) a->kick(r);
    }
  };
  // TAG's strike on the mech its beam marked (FIX (bug), weapons.md "TAG"):
  // the hit node's damage handler is a mech's: its live position and velocity.
  c.strike_target = [](Handle node, zwep::LockRef* out) {
    zwep::DamageFn fn = nullptr;
    Handle obj = zwep::damage_object(node, &fn);
    if (!obj || fn != mech_damage || !skis_calls().position) return false;
    Mech* m = mech_of(obj);
    out->entity = obj;
    out->at = reinterpret_cast<const zwep::Vec3f3*>(skis_calls().position(*m));
    out->velocity = reinterpret_cast<const zwep::Vec3f3*>(&m->velocity);
    return out->at != nullptr;
  };
  c.node_position = [](Handle node, Vec3f* p) { return zcls::node_world_point(zn(node), &p->x); };  // 0x004ed650
  c.node_axis = [](Handle node) {  // node +0x38 +0x78: the world matrix's z row
    return reinterpret_cast<const Vec3f*>(static_cast<uint8_t*>(zn(node)->data) + 0x78);
  };

  c.view_width = [] {
    PlayerView* v = program_view();
    auto* n = v ? static_cast<zcls::Node*>(v->camera) : nullptr;
    if (!n || !n->data) return 1.0f;
    const auto* d = static_cast<const zcls::Camera*>(n->data);
    // 4:3's half angle across is 4/3 the half angle down (0x0040d590).
    const double t43 = std::tan(4.0 / 3.0 * static_cast<double>(d->get<float>(0xf4)));
    return t43 > 0.0 ? static_cast<float>(std::tan(static_cast<double>(d->get<float>(0xf0))) / t43) : 1.0f;
  };

  zwep::RunHooks& r = zwep::run_hooks();
  r.parent_count = [](zwep::Handle n) { return zn(n)->parent_count(); };
  r.first_parent = [](zwep::Handle n) -> zwep::Handle { return zn(n)->parent_count() ? zn(n)->parents[0] : nullptr; };
  r.child_count = [](zwep::Handle n) { return zn(n)->child_count(); };
  r.first_child = [](zwep::Handle n) -> zwep::Handle { return zn(n)->child_count() ? zn(n)->children[0] : nullptr; };
  r.parent = [](zwep::Handle n, int32_t i) -> zwep::Handle { return zn(n)->parents[i]; };
  r.child = [](zwep::Handle n, int32_t i) -> zwep::Handle { return zn(n)->children[i]; };
  r.add_child = [](zwep::Handle p, zwep::Handle c) { zcls::node_add_child(zn(p), zn(c)); };
  r.remove_child = [](zwep::Handle p, zwep::Handle c) { zcls::node_remove_child(zn(p), zn(c)); };
  r.obj_add_child = [](zwep::Handle p, zwep::Handle c) { zcls::object3d_add_child(zn(p), zn(c)); };
  r.obj_remove_child = [](zwep::Handle p, zwep::Handle c) { zcls::object3d_remove_child(zn(p), zn(c)); };
  r.clone = [](zwep::Handle n) -> zwep::Handle { return zcls::tree_copy(zn(n), 0, 1); };
  r.link = [](zwep::Handle n) -> zwep::Handle { return zn(n)->owner; };                    // +0x40
  r.set_link = [](zwep::Handle n, zwep::Handle next) { zn(n)->owner = next; };
  r.set_shootable = [](zwep::Handle n, zwep::Projectile* p) {  // FLYOUT_HEALTH
    zcls::Node* node = zn(n);
    if (!p) {
      node->set_flags(node->flags() & ~0x8000000u);
      return;
    }
    node->set_flags(node->flags() | 0x8000000u);
    node->damage = reinterpret_cast<void*>(uintptr_t{1});
    node->owner = p;
  };
  r.clear_handler = [](zwep::Handle n) { zn(n)->damage = nullptr; };
  r.handler = [](zwep::Handle n) { return reinterpret_cast<uintptr_t>(zn(n)->damage); };
  r.set_handler = [](zwep::Handle n, uintptr_t h) { zn(n)->damage = reinterpret_cast<void*>(h); };
  r.node_frame = [](zwep::Handle n) { return zn(n)->get<int32_t>(0xc4); };
  r.node_flags = [](zwep::Handle n) { return zn(n)->flags(); };
  r.set_active = [](zwep::Handle n, int32_t on) { zcls::node_set_active(zn(n), on); };
  r.set_name = [](zwep::Handle n, const char* s) { zcls::node_set_name(zn(n), s); };
  r.set_flag_4eb620 = [](zwep::Handle n, int32_t on) { zcls::node_set_flag8(zn(n), on); };
  r.set_flag_4eb750 = [](zwep::Handle n, int32_t on) { zcls::node_set_intersect_bbox(zn(n), on); };
  r.set_flag_4eb790 = [](zwep::Handle n, int32_t on) { zcls::node_set_proximity(zn(n), on); };
  r.node_free = [](zwep::Handle n) { zcls::tree_destroy(zn(n)); };
  r.node_destroy = [](zwep::Handle n) { zcls::node_delete(zn(n)); };
  r.object3d_create = []() -> zwep::Handle { return zcls::object3d_create(); };
  r.set_scale = [](zwep::Handle n, float x, float y, float z) { zcls::object3d_set_scale(zn(n), x, y, z); };
  r.set_rotation = [](zwep::Handle n, float x, float y, float z) { zcls::object3d_set_rotation(zn(n), x, y, z); };
  r.set_translation = [](zwep::Handle n, float x, float y, float z) { zcls::object3d_set_translation(zn(n), x, y, z); };
  r.rotate = [](zwep::Handle n, float x, float y, float z) { zcls::object3d_add_rotation(zn(n), x, y, z); };
  r.set_uniform_scale = [](zwep::Handle n, float v) { zcls::object3d_set_opacity(zn(n), v); };  // 0x004fba20
  r.set_flag_4fbb90 = [](zwep::Handle n, int32_t on) { zcls::object3d_use_opacity(zn(n), on); };
  r.light_create = []() -> zwep::Handle { return zcls::light_create(); };                  // 0x004fa720
  r.light_position = [](zwep::Handle l, float x, float y, float z) { zcls::light_set_position(zn(l), x, y, z); };
  r.light_range = [](zwep::Handle l, float a, float b) { zcls::light_set_range(zn(l), a, b); };
  r.light_color = [](zwep::Handle l, float cr, float cg, float cb) { zcls::light_set_color(zn(l), cr, cg, cb); };
  r.world_add_light = [](zwep::Handle l) {
    if (qworld()) zcls::world_add_light(qworld(), zn(l));
  };
  r.world_remove_light = [](zwep::Handle l) {
    if (qworld()) zcls::world_remove_light(qworld(), zn(l));
  };
  // The surface marks' impact type ([0x0074a2b0]), read only by 0x00542060.
  // Craters and quicksand are zDEClient's.
  r.impact_type = zmod::mark_set_type;  // 0x005426a0
  r.crater_defaults = zdec::crater_defaults;        // 0x004ff920
  r.crater_make = zdec::crater_make;                // 0x004ffaa0
  r.quicksand_defaults = zdec::quicksand_defaults;  // 0x005020f0
  r.quicksand_make = [](const zwep::QuicksandOrder* q) { zdec::quicksand_make(q); };  // 0x00502110
  r.pool_live = []() { return !zcls::nodes().blocks.empty(); };                            // 0x004f8f50: [0x0060bf48]
  // The animations (zeffect): started at a node, following one, on a model.
  r.anim_attached = [](zwep::Handle a, zwep::Handle n, const void* off) -> zwep::Handle {  // 0x00508da0
    note_anim(a);
    return a ? zeff::anim_start_connector(anim_of(a), nullptr, zn(n), static_cast<const float*>(off), nullptr)
             : nullptr;
  };
  r.anim_follow = [](zwep::Handle a, zwep::Handle n) -> zwep::Handle {                    // 0x00508ee0
    note_anim(a);
    return a ? zeff::anim_start_connector_at(anim_of(a), nullptr, zn(n), nullptr, zn(n), nullptr) : nullptr;
  };
  r.anim_model = [](zwep::Handle a, zwep::Handle m) -> zwep::Handle {                     // 0x00508c10
    note_anim(a);
    return a ? zeff::anim_start(anim_of(a), zn(m), 0.0f, 0.0f, 0.0f) : nullptr;
  };
  r.anim_on_done = [](zwep::Handle a, zwep::Projectile* p) {                              // 0x00508f00 with 0x00583120
    if (a)
      zeff::anim_set_callback(
          anim_of(a), [](zeff::AnimDef*, void* arg, int32_t) { zwep::model_anim_done(static_cast<zwep::Projectile*>(arg)); },
          p);
  };
  r.anim_stop = [](zwep::Handle a) { zeff::anim_restart(anim_of(a)); };                   // 0x00508460: to stopping
  r.anim_free = [](zwep::Handle a) { zeff::anim_stop(anim_of(a)); };                      // 0x00506ca0
  r.effect_handle = [](zwep::Handle a, const float v[9]) -> zwep::Handle {                // 0x00508aa0
    note_anim(a);
    return a ? zeff::anim_start_at(anim_of(a), nullptr, v, v + 3, v + 6) : nullptr;
  };
  // zSound.
  r.sound_at = [](zwep::Handle s, const zwep::Vec3f3* at, float volume) -> zwep::Handle {  // 0x0056e7e0
    note_sound(s);
    return s ? zsnd::play_at(static_cast<zsnd::Object*>(s), &at->x, volume, nullptr) : nullptr;
  };
  r.sound_stop = [](zwep::Handle v) { if (v) zsnd::stop(static_cast<zsnd::VoiceBase*>(v)); };  // 0x0056e9f0
  r.sound_frequency = [](zwep::Handle v, float t) {                                         // 0x0056f280
    if (v) zsnd::set_frequency(static_cast<zsnd::VoiceBase*>(v), t);
  };
  // 0x004eda10 on the world: the ground below a guided shot (one record kept).
  r.ground = [](const zwep::Vec3f3* at, zwep::HitList* out) {
    static zcls::GroundHits g;
    out->count = 0;
    if (!qworld()) return;
    zcls::ground_below(qworld(), &at->x, &g);
    out->count = g.count;
    if (g.count) std::memcpy(&out->rec[0], &g.hit[0], sizeof g.hit[0]);
  };
  r.query_static = [](int32_t on) { zcls::segment_options().static_only = on; };           // 0x004ed9d0
  r.query_since = [](int32_t on, uint32_t frame) {                                         // 0x004ed9e0
    zcls::segment_options().since = on ? static_cast<int32_t>(frame) : -1;
  };
  r.query_mask = [](uint32_t mask) { zcls::segment_options().exclude = mask; };            // 0x004eda00
  r.segment_list = segment_list;                                                            // 0x004eed90
  r.segment = segment_nearest;                                                              // 0x004eece0

  zwep::HitHooks& h = zwep::hit_hooks();
  h.node_handler = [](zwep::Handle n) { return reinterpret_cast<uintptr_t>(zn(n)->damage); };  // +0xbc
  h.node_projectile = [](zwep::Handle n) { return static_cast<zwep::Projectile*>(zn(n)->owner); };
  h.handler_live = [](uintptr_t t) { return reinterpret_cast<zwep::NodeHandlers*>(t)->slot[0].fn != nullptr; };
  h.call_handler = [](uintptr_t t, zwep::WeaponDef* def, zwep::HitRecord* hit, float damage) {
    const auto& s = reinterpret_cast<zwep::NodeHandlers*>(t)->slot[0];
    return reinterpret_cast<zwep::DamageFn>(s.fn)(s.object, def, hit, damage);
  };
  // The impact's sound, animation turned to the surface, craters; zEffect's
  // table (empty in both games' effects.zrd); the surface marks not ported.
  h.sound = zwep::sound;                                                                    // 0x00582f60
  h.effect = [](zwep::Handle a, const float v[9]) {                                         // 0x00508aa0
    note_anim(a);
    if (a) zeff::anim_start_at(anim_of(a), nullptr, v, v + 3, v + 6);
  };
  h.surface_angles = [](const zwep::Vec3f3* n, float out[3]) {
    const float up[3] = {0.0f, 1.0f, 0.0f};  // 0x005eda70
    float q[4];
    zmth::quat_between(up, &n->x, q);  // 0x00525a50
    zmth::quat_angles(q, out);         // 0x00525f80
  };
  h.crater = [](zwep::WeaponDef* d, zwep::HitRecord* hit, int32_t, zwep::Projectile* p) {  // 0x005825b0
    return zwep::crater(d, hit, p);
  };
  h.quicksand = [](zwep::WeaponDef* d, zwep::HitRecord* hit, int32_t soil, zwep::Projectile* p) {  // 0x005824b0
    return zwep::quicksand(d, hit, soil, p), 0;
  };
  h.impact_effect = [](int32_t e, const zwep::Vec3f3*) { zeff::effect_spawn(e); };        // 0x0050d650
  // 0x00542060: nothing unless a script gave a mark image (none ships); the
  // painting into the hit texture is not ported (weapon-effects.md).
  h.surface_mark = [](zwep::HitRecord*) {
    if (zmod::mark_on()) unported("0x00542060");
  };
  h.soil = [](zwep::Handle m) { return m ? static_cast<int32_t>(static_cast<zmod::Material*>(m)->soil) : 0; };
  h.node_intersect = [](zwep::Handle n) { return (zn(n)->flags() & 0x10) != 0; };
  h.set_intersect = [](zwep::Handle n, int32_t on) { zcls::node_set_intersect(zn(n), on); };
  h.query = [](zwep::Handle w, const zwep::Vec3f3* at, float radius, zwep::HitList* out) {  // 0x004f0ac0
    static zcls::LineHits r;
    zcls::Node* node = w ? zn(w) : qworld();
    if (!node) return out->count = 0, 1;
    const int32_t res = zcls::sphere_query(node, &at->x, radius, nullptr, 1, 1, &r);
    to_hits(r, out);
    return res;
  };

  heat_calls().heat_option = [] { return heat() != 0; };  // 0x0040cfc0, the Heat option
  // 0x004b0de0(-1, 1) on +0xd4 (0x00419b5a): riding out a heat shutdown, an
  // ammo bin may cook off.
  heat_calls().ammo_explosion = [](Mech& m) {
    auto* wm = static_cast<WeaponManager*>(m.ammo);
    return wm && wm->critical(-1, true);
  };
  // 0x00465ea0 on the torso (+0x39c): the aim (Mech::aim_at, the AI's).
  aim_calls().torso_aim = [](Mech& m, const Vec3* dir, void* out, int on) -> int {
    Vec3 scratch;  // the exe's callers pass a buffer; the AI's aim here passes none
    return m.torso && m.torso->set_aim(dir, out ? static_cast<Vec3*>(out) : &scratch, on != 0) ? 1 : 0;
  };
  // The arm restriction's (0x004b2da0): the view, its camera's +0x40 (0 and
  // 1 the side views), the controls' view state (+0x37c +0x0c, 1 free look),
  // the torso's turned rotation (0x00465940) and its view (+0xa4 while +0xa2).
  WeaponCalls& wc = weapon_calls();
  wc.view = []() -> Handle {
    int32_t look = 0;
    static int32_t kept = 0;
    return program_view_look(&look) ? (kept = look, static_cast<Handle>(&kept)) : nullptr;
  };
  wc.view_kind = [](Handle v) { return v ? *static_cast<const int32_t*>(v) : -1; };
  wc.control_mode = [](Handle owner) -> int32_t {
    const Mech* m = entity_is_mech(owner) ? static_cast<const Mech*>(owner) : nullptr;
    return m && m->controls ? m->controls->view : 0;
  };
  wc.torso_aim = [](Handle owner) -> const zmth::Quat* {
    static zmth::Quat q;
    const Mech* m = entity_is_mech(owner) ? static_cast<const Mech*>(owner) : nullptr;
    if (!m || !m->torso) return nullptr;
    float f[4];
    m->torso->shake_q(f);
    q.w = f[0], q.x = f[1], q.y = f[2], q.z = f[3];
    return &q;
  };
  wc.torso_look = [](Handle owner) -> const zmth::Quat* {
    static zmth::Quat q;
    const Mech* m = entity_is_mech(owner) ? static_cast<const Mech*>(owner) : nullptr;
    if (!m || !m->torso || !m->torso->view_aim) return nullptr;
    q.w = m->torso->view_q[0], q.x = m->torso->view_q[1], q.y = m->torso->view_q[2], q.z = m->torso->view_q[3];
    return &q;
  };
  update_calls().weapons_changed = [](Mech& m) {  // 0x004b2da0 on +0xd4
    auto* wm = static_cast<WeaponManager*>(m.ammo);
    return wm && wm->arm_restriction();
  };
  wc.part_in_water = [](Handle part) { return static_cast<const MechPart*>(part)->submerged; };  // +0x49
  // vt+0x58: 0x0040bae0 (0) for the mech, the basic vehicle, the field base
  // and the turret; only a network game's classes answer otherwise.
  wc.owner_x58 = [](Handle) -> int32_t { return 0; };
  collide_calls().remote = [](Mech& m) { return m.remote; };  // a network mech's (vt+0x5c)
  // The vehicle builder's (0x004b4660) firepoint lookups.
  wc.node_find = [](Handle node, const char* name) -> Handle {  // 0x004eb0d0
    return zcls::node_find(static_cast<zcls::Node*>(node), name);
  };
  wc.node_by_name = [](Handle node, const char* name) -> Handle { return node_find_prefix(node, name); };  // 0x004abb60
  // The aim's view test (0x0041bc20): [0x005f4c8c] +0x64 is the mech and its
  // camera (+0x58) +4 is 1, the cockpit.
  aim_calls().cockpit_view = [](Mech& m) {
    return program_view_target() == static_cast<void*>(&m) && program_view_mode_id() == 1;
  };

  // Mech3.exe 1.2 @ 0x0040ea10 / 0x0040e9d0 (through 0x0040ef30 / 0x0040ef10):
  // what a node hit belongs to, by its +0x28 id and +0x40 owner (the build
  // marks each part's subtree, mech_part_build.cpp).
  DamageCalls& d = damage_calls();
  d.node_part = [](Handle n) -> MechPart* {
    zcls::Node* z = zn(n);
    return z && (z->get<uint32_t>(0x28) & 0xff) ? static_cast<MechPart*>(z->owner) : nullptr;
  };
  d.node_owner = node_owner;
  d.attacker = [] { return node_entity(zwep::hit_state().attacker); };  // 0x0040eee0 on [0x009a0edc]

  UpdateCalls& u = update_calls();
  u.key = [](int cmd) { return zin::command(cmd); };  // 0x0051d300: the view key (CMD 0x17), torso and free look
  u.weapons_fire = [](Mech& m, uint8_t fire) {  // 0x004b0c20 on +0xd4
    if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_trigger(fire != 0);
  };
  u.weapons_update = [](Mech& m, float dt) {  // 0x004b0c30 on +0xd4
    if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->update(dt);
  };
}

// The frame's step [0x0072c52c] into each module's copy of it, once a frame
// before the vehicles update (the exe has one global).
void sync_frame_step(float dt, float clock) {
  mech3::world().dt = dt;
  weapon_world().dt = dt;
  weapon_world().clock = clock;  // [0x0072c530], the timer's scaled total: the AMS's and the turrets' clock
  zwep::hit_state().dt = dt;
}

int32_t mech_hits_taken(void** attacker_node) {
  if (attacker_node) *attacker_node = g_last_attacker;
  return g_hits;
}

void* mech_node(void* mech) { return mech ? static_cast<Mech*>(mech)->node : nullptr; }

bool mech_group_reloading(void* mech, int32_t g) {
  auto* wm = static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo);
  if (!wm || g < 0 || g > 4 || !wm->groups[g]) return false;
  for (uint32_t i = 0; i < wm->groups[g]->count; ++i)
    if (const Weapon* w = wm->groups[g]->weapons[i]; w && (w->reload > 0.0f || w->is_firing())) return true;
  return false;
}

bool program_mech_ams(void* mech, int32_t* fitted, bool* on, int32_t* rounds) {
  auto* m = static_cast<Mech*>(mech);
  if (!m || !m->ams) return false;
  *fitted = m->equipment[0], *on = m->ams->on, *rounds = m->ams->rounds;
  return true;
}
void program_mech_ams_set(void* mech, bool on) {
  auto* m = static_cast<Mech*>(mech);
  if (!m || !m->ams) return;
  if (on && m->equipment[0] < 1) m->equipment[0] = 1, m->ams->rounds = 100;  // one fitted, as the loadout's (0x004b3ab0)
  m->ams->on = on;
}
int32_t program_fire_at(void* mech, const char* weapon, int32_t n, float dist) {
  auto* m = static_cast<Mech*>(mech);
  zwep::WeaponDef* def = zwep::find_def(weapon);
  if (!m || !def || !m->transform) return 0;
  const float tx = m->transform[9], ty = m->transform[10] + m->height * 0.5f, tz = m->transform[11];
  int32_t fired = 0;
  for (int32_t i = 0; i < n; ++i) {
    const Vec3f from{tx + 3.0f * static_cast<float>(i - n / 2), ty + 15.0f, tz + dist};
    Vec3f dir{tx - from.x, ty - from.y, tz - from.z};
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir = {dir.x / len, dir.y / len, dir.z / len};
    const Vec3f still{0, 0, 0};
    if (spawn_projectile(def, nullptr, nullptr, &from, &dir, &still, 0, 0, true)) ++fired;  // hostile to the player
  }
  return fired;
}

// What F1..F5 select (CMD_WEPGRP_1..5 through the mech's command handler,
// not ported): the first group holding a weapon that heats its mech; -1 none.
std::string mech_group_names(void* mech, int32_t g) {
  auto* wm = static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo);
  std::string s;
  for (uint32_t i = 0; wm && g >= 0 && g < 5 && wm->groups[g] && i < wm->groups[g]->count; ++i)
    if (const Weapon* w = wm->groups[g]->weapons[i]) s += std::string(s.empty() ? "" : " ") + (w->def ? w->def->name : "?");
  return s;
}
int32_t mech_group_ammo(void* mech, int32_t g) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  int32_t n = 0;
  for (uint32_t i = 0; wm && g >= 0 && g < 5 && wm->groups[g] && i < wm->groups[g]->count; ++i)
    if (Weapon* w = wm->groups[g]->weapons[i]) n += w->ammo();  // vt+0x14
  return n;
}
float mech_group_ready(void* mech, int32_t g) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  float r = 1.0f;
  for (uint32_t i = 0; wm && g >= 0 && g < 5 && wm->groups[g] && i < wm->groups[g]->count; ++i)
    if (const Weapon* w = wm->groups[g]->weapons[i]) r = std::min(r, w->ready());
  return r;
}
bool mech_group_muzzle(void* mech, int32_t g, float muzzle[3], float dir[3]) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  if (!wm || g < 0 || g > 4 || !wm->groups[g] || wm->groups[g]->count == 0 || !wm->groups[g]->weapons[0]) return false;
  const Weapon* w = wm->groups[g]->weapons[0];
  muzzle[0] = w->muzzle.x, muzzle[1] = w->muzzle.y, muzzle[2] = w->muzzle.z;
  dir[0] = w->direction.x, dir[1] = w->direction.y, dir[2] = w->direction.z;
  return true;
}
bool mech_group_beam(void* mech, int32_t g, BeamProbe* out) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  if (!wm || g < 0 || g > 4 || !wm->groups[g] || wm->groups[g]->count == 0 || !wm->groups[g]->weapons[0]) return false;
  Weapon* w = wm->groups[g]->weapons[0];
  if (!w->is_beam()) return false;
  *out = BeamProbe{};
  out->firing = w->is_firing();
  out->ready = w->ready();
  out->trigger_down = wm->trigger_down, out->trigger_was = wm->trigger_was, out->state = wm->state;
  if (auto* b = static_cast<zwep::Beam*>(static_cast<BeamWeapon*>(w)->beam)) {
    out->beam_active = b->active != 0;
    if (b->origin) out->origin[0] = b->origin->x, out->origin[1] = b->origin->y, out->origin[2] = b->origin->z;
  }
  Vec3f mz{}, d{};
  if (w->aim(nullptr, &mz, &d, nullptr)) out->live[0] = mz.x, out->live[1] = mz.y, out->live[2] = mz.z;
  return true;
}
float mech_arm_recoil(void* mech) {
  float r = 0.0f;
  if (Mech* m = static_cast<Mech*>(mech))
    for (Arm* a : m->arms)
      if (a && a->recoil > r) r = a->recoil;
  return r;
}
bool mech_lock_shown(void* mech) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  return wm && wm->has_target;
}
void* mech_target_object(void* mech) {
  const Mech* m = static_cast<const Mech*>(mech);
  return m && m->target ? hud_contact_vehicle(m->target) : nullptr;
}
bool mech_select_group(void* mech, int32_t g) {
  auto* wm = static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo);
  return wm && wm->select_group(g);
}
int32_t mech_current_group(void* mech) {
  auto* wm = mech ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  return wm ? wm->group : -1;
}
int32_t mech_select_hot_group(void* mech) {
  auto* wm = static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo);
  if (!wm) return -1;
  for (int32_t g = 0; g < 5; ++g)
    for (uint32_t i = 0; wm->groups[g] && i < wm->groups[g]->count; ++i) {
      const Weapon* w = wm->groups[g]->weapons[i];
      if (w && w->def && w->def->type && w->def->type->heat > 0.0f) return wm->select_group(g) ? g : -1;
    }
  return -1;
}

// Mech3.exe 1.2 @ 0x0041aba0, the player tick's tail on the pilot's mech:
// its target's chosen part's point (0x0041d6f0), the ammo gauge (0x004b0d80)
// and the last attacker cleared.
void mech_after_tick(void* mech, uint32_t (*)(const char*)) {
  Mech* m = static_cast<Mech*>(mech);
  m->aim_part();
  if (auto* wm = static_cast<WeaponManager*>(m->ammo)) wm->ammo_gauge();
  m->attacker = nullptr;
}

// Everything wire_weapons set, back to the defaults.
void unwire_weapons() {
  weapon_calls() = WeaponCalls{};
  aim_calls() = AimCalls{};
  zwep::run_hooks() = zwep::RunHooks{};
  zwep::hit_hooks() = zwep::HitHooks{};
  update_calls().weapons_fire = nullptr;
  update_calls().weapons_update = nullptr;
  heat_calls().heat_option = nullptr;
  damage_calls().node_part = nullptr;
  damage_calls().node_owner = nullptr;
  damage_calls().attacker = nullptr;
}

// Mech3.exe 1.2 @ 0x005833b0(mech, node, 1, 0x004161a0): the mech's node
// takes hits into Mech::TakeDamage.
void mech_set_damage_handler(void* mech) {
  Mech* m = static_cast<Mech*>(mech);
  zwep::set_damage_handler(m, m->node, mech_damage, 1);
}

}  // namespace mech3
