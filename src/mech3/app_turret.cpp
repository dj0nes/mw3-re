#include "mech3/app_mech.h"
#include "mech3/app_turret.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_init.h"
#include "mech3/entity_bind.h"
#include "mech3/zai_anim_owner.h"
#include "gamez/zentity/zent_init.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/ztwod/twod.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/app_view.h"
#include "mech3/player_view.h"
#include "mech3/mission_world.h"
#include "mech3/sensors.h"
#include "mech3/turret.h"
#include "mech3/weapon.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_target.h"

// The turrets in the program (docs/vehicles.md, The turrets in the
// program): turret.cpp's calls out wired to the ports. Ours, not the exe's.
namespace mech3 {

void* node_entity(void* node);  // app_weapons.cpp's 0x0040e920

namespace {
using tur::Turret;
using zgame::Handle;

MechWiring g_w;
std::set<const Turret*> g_turrets;              // this mission's, alive
std::map<const Turret*, zwep::LockRef> g_locks; // each one's shot lock (its +0x1a4)
std::map<Handle, zwep::Vec3f3> g_places;        // the entities' places a pick aimed at (0x005a8630's)

Handle hd(const void* p) { return p && g_w.handle_of ? g_w.handle_of(g_w.engine, p) : 0; }
void* ob(Handle h) { return h && g_w.object_of ? g_w.object_of(g_w.engine, h) : nullptr; }
zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(ob(h)); }
float* fl(Handle h) { return static_cast<float*>(ob(h)); }

// The weapons callback 0x004a7940: a hit on a turret's node.
uint32_t turret_damage_handler(zwep::Handle object, zwep::WeaponDef*, zwep::HitRecord*, float damage) {
  return static_cast<Turret*>(object)->hit(0, 0, damage);
}

bool prefixed(zcls::Node* n) {
  const char* key = static_cast<const char*>(zcls::list_walk().key);
  return std::strncmp(n->name(), key, std::strlen(key)) == 0;
}

// 0x004eb0d0: the node itself, then its children first to last, depth first.
zcls::Node* find_first(zcls::Node* n, const char* name) {
  if (!n || !name) return nullptr;
  if (std::strcmp(n->name(), name) == 0) return n;
  for (int32_t i = 0; n->children && i < n->child_count(); ++i)
    if (zcls::Node* f = find_first(n->children[i], name)) return f;
  return nullptr;
}

struct WiredTurrets : tur::TurretHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  Handle turret_handle(const void* mem) override { return hd(mem); }
  Handle marker_create(const char* name) override { return hd(new ztwod::Image(name, 0, 0, 1)); }  // 0x0059b830
  void marker_delete(Handle m) override { delete static_cast<ztwod::Image*>(ob(m)); }
  void report(int32_t line, const char* fmt, const char* name) override {
    char text[256];
    std::snprintf(text, sizeof text, fmt, name ? name : "?");
    std::fprintf(stderr, "mw3: report 0x200 turret.cpp:%d %s\n", line, text);
  }
  // zClass.
  Handle nodes_first(const char* prefix) override { return hd(zcls::list_walk_next(prefix, 6, prefixed)); }
  Handle nodes_next() override { return hd(zcls::list_walk_next(nullptr, 6, prefixed)); }
  const char* node_name(Handle n) override { return zn(n) ? zn(n)->name() : nullptr; }
  uint32_t node_flags(Handle n) override { return zn(n) ? zn(n)->flags() : 0; }
  Handle node_model(Handle n) override { return zn(n) ? hd(zn(n)->model) : 0; }
  Handle node_find(Handle root, const char* name) override { return hd(zcls::node_find_in_tree(zn(root), name)); }
  Handle node_child(Handle n, const char* name) override { return hd(find_first(zn(n), name)); }
  Handle node_named(const char* name) override { return hd(zcls::list_find_by_name(6, name)); }
  void node_set_active(Handle n, int32_t on) override { zcls::node_set_active(zn(n), on); }
  void node_set_box_hit(Handle n, int32_t on) override { zcls::node_set_intersect_bbox(zn(n), on); }
  void node_world_position(Handle n, float out[3]) override {
    out[0] = out[1] = out[2] = 0;
    zcls::node_world_point(zn(n), out);
  }
  void node_rotation(Handle n, float out[3]) override { zcls::object3d_get_rotation(zn(n), &out[0], &out[1], &out[2]); }
  void node_translation(Handle n, float out[3]) override {
    zcls::object3d_get_translation(zn(n), &out[0], &out[1], &out[2]);
  }
  Handle node_matrix(Handle n) override { return hd(zcls::object3d_matrix(zn(n))); }
  float matrix_y(Handle m) override { return fl(m) ? fl(m)[10] : 0.0f; }
  float matrix_at(Handle m, int32_t i) override { return fl(m) ? fl(m)[i] : 0.0f; }
  void matrix_put(Handle m, int32_t i, float v) override {
    if (fl(m)) fl(m)[i] = v;
  }
  void node_set_translation(Handle n, float x, float y, float z) override { zcls::object3d_set_translation(zn(n), x, y, z); }
  void node_set_matrix(Handle n, Handle m) override { zcls::object3d_set_matrix(zn(n), fl(m)); }
  int32_t node_box(Handle n, float box[6]) override {  // 0x004ebd50: 0 when it has one
    const float* b = zcls::node_box(zn(n));
    if (!b) return 1;
    std::memcpy(box, b, 6 * sizeof(float));
    return 0;
  }
  void nodes_pending() override { zcls::update_changed(); }
  void node_world_cached(Handle n, float out[3]) override {
    out[0] = out[1] = out[2] = 0;
    zcls::node_world_position(zn(n), out);
  }
  // The matrix stack.
  zmth::Mat34 frame_buf;
  void matrix_push() override { zmth::push(&frame_buf); }
  void matrix_identity() override { zmth::load_identity(); }
  void node_to_world(Handle n) override { zcls::node_to_world(zn(n), 3); }
  void point_through(float v[3]) override { zmth::transform_points(reinterpret_cast<zmth::Vec3*>(v), 1); }
  void dir_through_inverse(float v[3]) override { zmth::rotate_points_transposed(reinterpret_cast<zmth::Vec3*>(v), 1); }
  void matrix_pop() override { zmth::pop(); }
  // zEffect, zSound.
  Handle anim_find(const char* name) override { return name ? hd(zeff::anim_find(name)) : 0; }
  uint8_t anim_claim(Handle self, Handle a) override { return ai::anim_owner_claim(self, a) ? 1 : 0; }  // 0x004d4f20
  // The target view's red (0x004fb8c0 with [0x005e8398]) and its switch (0x004fb840);
  // the EFFECT's texture scroll (0x00526ae0(1, 10, 0)) and its step (0x00541ad0).
  void node_tint(Handle n, float amount) override {
    static const float kRed[3] = {1.0f, 0.0f, 0.0f};  // [0x005e8398]
    zcls::object3d_set_blend(zn(n), kRed, amount);
  }
  void node_tinted(Handle n, int32_t on) override { zcls::object3d_use_blend(zn(n), on); }
  void model_scroll(Handle m) override { zmod::model_set_scroll(static_cast<zmod::Model*>(ob(m)), 1, 10.0f, 0.0f); }
  void effect_animate(Handle m) override { zmod::model_scroll_tick(static_cast<zmod::Model*>(ob(m))); }
  void anim_reset(Handle a, Handle n) override {
    if (auto* d = static_cast<zeff::AnimDef*>(ob(a))) zeff::anim_restart(d, zn(n));
  }
  void anim_run(Handle a, Handle n) override {
    if (auto* d = static_cast<zeff::AnimDef*>(ob(a))) zeff::anim_start(d, zn(n), 0, 0, 0);
  }
  Handle sound_find(const char* name) override { return name ? hd(zsnd::find(name)) : 0; }
  void sound_play(Handle s, const float* at) override {
    if (auto* o = static_cast<zsnd::Object*>(ob(s))) zsnd::play_at(o, at, 1.0f, nullptr);
  }
  // zWeapon.
  static zwep::WeaponDef* def(Handle h) { return static_cast<zwep::WeaponDef*>(ob(h)); }
  Handle weapon_def(const char* name) override { return name ? hd(zwep::find_def(name)) : 0; }
  float weapon_range(Handle d) override { return def(d) ? def(d)->range : 0.0f; }
  uint32_t weapon_flags(Handle d) override { return def(d) ? def(d)->flags : 0; }
  float weapon_lead(Handle d) override { return def(d) ? def(d)->gravity : 0.0f; }
  uint32_t weapon_fire_flags(Handle d) override { return def(d) ? def(d)->fire.flags : 0; }
  Handle beam_make(Handle d, Handle n, Handle, const float* from, const float* dir) override {
    return hd(zwep::beam_make(def(d), zn(n), nullptr, reinterpret_cast<const zwep::Vec3f3*>(from),
                              reinterpret_cast<const zwep::Vec3f3*>(dir), 2));
  }
  Handle beam_free(Handle b) override { return zwep::beam_free(static_cast<zwep::Beam*>(ob(b))) ? b : 0; }
  void beam_start(Handle b) override { zwep::beam_start(static_cast<zwep::Beam*>(ob(b))); }
  void beam_stop(Handle b) override { zwep::beam_stop(static_cast<zwep::Beam*>(ob(b))); }
  void detonate(Handle d, const float* at) override { zwep::detonate(def(d), reinterpret_cast<const zwep::Vec3f3*>(at), nullptr); }
  float aim_slope(Handle d, const float* from, Handle to) override {
    float dist = 0;
    return zwep::aim_slope(def(d), reinterpret_cast<const zwep::Vec3f3*>(from),
                           reinterpret_cast<const zwep::Vec3f3*>(ob(to)), &dist);
  }
  void shooter_set(Turret& t, bool on) override {
    if (!on) return zwep::set_lock(nullptr, nullptr);
    zwep::LockRef& l = g_locks[&t];
    l.entity = ob(t.shooter);
    l.at = reinterpret_cast<const zwep::Vec3f3*>(ob(t.target_at));
    l.velocity = nullptr;
    zwep::set_lock(&t.x1a0, &l);
  }
  void attach_fire(Handle d, Handle n) override { zwep::attach_fire(def(d), zn(n)); }
  void weapon_fire(Handle d, Handle n, const float* from, const float* dir, const float* v, int32_t hostile) override {
    spawn_projectile(def(d), zn(n), nullptr, reinterpret_cast<const Vec3f*>(from), reinterpret_cast<const Vec3f*>(dir),
                     reinterpret_cast<const Vec3f*>(v), 0, 0, hostile != 0);
  }
  int32_t set_hit_callback(Handle self, Handle n) override {
    return zwep::set_damage_handler(ob(self), zn(n), turret_damage_handler, 1);
  }
  void remove_handlers(Handle n) override { zwep::remove_handlers(zn(n)); }
  Handle hit_attacker() override { return hd(zwep::attacker()); }
  Handle node_object(Handle n) override { return hd(node_entity(ob(n))); }
  void hit_killed() override { zwep::note_kill(reinterpret_cast<zwep::Handle>(1), nullptr); }
  void hit_health(float f) override { zwep::set_health_after(f); }
  // obj.vt+0x80: a mech's kill credit (0x0041b420; a turret is its
  // "building", vt+0x48); anything else's does nothing.
  void credit_kill(Handle object, Handle self) override { program_mech_kill_credit(ob(object), ob(self)); }
  Handle player_vehicle() override {
    const uint32_t p = g_w.pilot ? g_w.pilot(g_w.engine) : 0;
    return p ? g_w.peek32(g_w.engine, p + 0x14) : 0;
  }
  int32_t zones_meet(Handle player, const Turret& t) override {
    uint8_t mine[4];
    if (!program_entity_zones(ob(player), mine)) return 0;
    return zmod::zones_overlap(mine, t.zones);
  }
  // zEntity3d and AiTarget.
  void entity_init(Handle) override {}
  void entity_end(Handle) override {}
  void entity_list(Handle self) override {
    ai::entity_list().push_back(self);
    g_turrets.insert(static_cast<Turret*>(ob(self)));
  }
  void entity_unlist(Handle self) override {
    auto& l = ai::entity_list();
    l.erase(std::remove(l.begin(), l.end(), self), l.end());
  }
  // 0x004d6c90: the old node unbound (0x0040ed00), the markers, the new one
  // bound to the turret (0x0040eaa0: its vt+0x48 is itself, bit 17).
  void entity_set_node(Handle self, Handle n, Handle* target_node, Handle* centroid_node) override {
    auto* turret = static_cast<Turret*>(ob(self));
    if (turret && turret->node) entity_unbind(zn(turret->node), turret, entity_unbind_calls());
    if (zn(n) && turret) {
      EntityCasts casts;
      casts.turret = turret;
      entity_bind(zn(n), turret, casts);
    }
    zcls::Node *c = nullptr, *t = nullptr;
    if (zn(n)) zent::marker_nodes(zn(n), nullptr, nullptr, &c, &t);  // 0x004d6c90 passes 0, 0
    *target_node = n ? hd(t ? t : zn(n)) : 0;
    *centroid_node = n ? hd(c ? c : zn(n)) : 0;
  }
  // zEntity3d 0x005a80f0 (vt+0x38): the zone record of the ground under the
  // node (from 10 up, the node's flag 8 off for the query; water skipped),
  // its id the one shared with the camera's zones (the caller's `at`), else
  // its first, onto the node.
  void entity_place(Handle self, Handle, Handle) override {
    Turret* t = static_cast<Turret*>(ob(self));
    zcls::Node* n = zn(t ? t->node : 0);
    zcls::Node* w = world_hooks().world;
    float p[3];
    if (!n || !w || zcls::node_world_position(n, p) != 0) return;
    zcls::node_set_flag8(n, 0);
    static zcls::GroundHits hits;
    const int32_t r = zcls::ground_point(w, p[0], p[1] + 10.0f, p[2], &hits);
    zcls::node_set_flag8(n, 1);
    int32_t i = 0;
    const int32_t count = r == 0 ? hits.count : 0;
    while (i < count && hits.hit[i].material && hits.hit[i].material->soil == 1) ++i;
    if (i >= count) {
      std::fprintf(stderr, "mw3: report 0x100 zent_init.cpp:125 No ground intersections for node \"%s\", zone information invalid.\n",
                   n->name());
      return;
    }
    std::memcpy(t->zones, &hits.hit[i].zone[0], 4);
    const PlayerView* v = program_view();
    const uint8_t* cam = v ? static_cast<const uint8_t*>(static_cast<zcls::Node*>(v->camera)->data) + 0x1e4 : nullptr;
    int32_t zone = cam ? zmod::zone_common(t->zones, cam) : -1;
    if (zone == -1) zone = t->zones[1];
    zcls::node_set_zone(n, static_cast<uint8_t>(zone));
  }
  Handle world_node() override { return hd(world_hooks().world); }
  void entity_markers(Handle) override {}  // 0x005a8500: the markers' world matrices, worked out when asked here
  void entity_refresh(Handle) override {}  // 0x005a85c0: the node's world place, likewise
  void target_message(Handle, uint32_t, int32_t) override {}  // 0x004d7270: a turret's vt+0x78 / vt+0x7c are empty
  // The frame's.
  void read_vec(Handle at, float out[3]) override {
    if (const float* p = fl(at)) std::memcpy(out, p, 3 * sizeof(float));
    else out[0] = out[1] = out[2] = 0;
  }
  uint32_t player_team(Handle player) override { return target_team(player); }  // its vt+0x24, vt+0x28: itself
  Handle contact_vehicle(const ai::Contact* c) override { return c ? c->seen : 0; }  // vt+0x2c: itself
  Handle vehicle_target(Handle v) override { return v; }                            // vt+0x28: itself
  uint32_t target_team(Handle t) override {
    const ai::AiTarget* a = ai::sensor_hooks().fields(t);
    return a ? a->team : 0;
  }
  bool line_clear(Handle self, Handle v) override { return ai::clear_line(self, v, nullptr); }
  // 0x005a8630: where the entity is aimed at (its target node), kept here so
  // the shot's lock can point at it.
  Handle entity_position(Handle e) override {
    float p[3];
    ai::brain_hooks().entity_lock_position(e, p);
    zwep::Vec3f3& at = g_places[e];
    at = {p[0], p[1], p[2]};
    return hd(&at);
  }
  Handle ams_target(Ams& a) override { return hd(a.target_at()); }
};
WiredTurrets g_hooks;

}  // namespace

void wire_turrets(const MechWiring* w) {
  g_w = w ? *w : MechWiring{};
  tur::set_turret_hooks(w ? &g_hooks : nullptr);
}

Turret* program_turret(const void* p) {
  auto* t = static_cast<const Turret*>(p);
  return p && g_turrets.count(t) ? const_cast<Turret*>(t) : nullptr;
}
Turret* program_turret_by_handle(uint32_t h) { return program_turret(ob(h)); }

void program_turret_dt(float dt) { weapon_world().dt = dt; }

bool program_turrets_load(const char* file) {
  g_turrets.clear();
  g_locks.clear();
  g_places.clear();
  return tur::turrets_load(file, 0);
}

// 0x0049dc40's walk: a turret goes through its vt+0x00 (0x004a5fd0).
bool program_turret_free(void* p) {
  Turret* t = program_turret(p);
  if (!t) return false;
  g_turrets.erase(t);
  g_locks.erase(t);
  auto& l = tur::turret_list();
  l.erase(std::remove(l.begin(), l.end(), t), l.end());
  delete t;
  if (g_w.forget_object) g_w.forget_object(g_w.engine, p);
  return true;
}

void program_turrets_end() {
  tur::turrets_end();
  g_places.clear();
}

void program_turret_frame(void* p) {
  if (Turret* t = program_turret(p)) t->frame();
}

void* program_turret_marker(int32_t side) { return ob(tur::turret_marker(side)); }

}  // namespace mech3
