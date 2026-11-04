#include "mech3/app_vehicle.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/znetwork/znet_object.h"
#include "gamez/ztwod/twod.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/app_mech.h"
#include "mech3/app_sensors.h"
#include "mech3/entity_bind.h"
#include "mech3/mech_build.h"
#include "mech3/app_view.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_fieldbase.h"
#include "mech3/mech_fieldbase_cut.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_names.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_waynet.h"
#include "mech3/zainetedit.h"
#include "gamez/zsound/zsnd.h"
#include "mech3/sound_extra.h"
#include "mech3/mission_world.h"
#include "mech3/sensors.h"
#include "mech3/veh_basic_object.h"
#include "mech3/veh_controls.h"
#include "mech3/veh_move.h"
#include "mech3/veh_stealth.h"
#include "mech3/game_editor.h"
#include "mech3/zrdraiv.h"
#include "gamez/zreader/zreader.h"
#include "mech3/weapon.h"
#include "mech3/zai_brain.h"

// The vehicles in the program (docs/vehicles.md, In the program): the
// basic vehicle's and its controls' calls out wired to the ports, and what
// the mission start (player.cpp) asks of a vehicle. Ours, not the exe's.
namespace mech3 {

void* node_entity(void* node);  // app_weapons.cpp's 0x0040e920

namespace {
MechWiring g_w;
std::vector<Vehicle*> g_order;        // the same, as made
std::set<const Vehicle*> g_vehicles;  // the program's, this mission's
Stealth* g_stealth = nullptr;         // [0x00609a2c], made at each mission load (veh_stealth)
zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(h); }

struct WiredVehicles : VehBasicHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  int32_t view_camera_id() override { return program_view_camera_id(); }  // [0x005f4c8c]+0x58 +0x04
  void view_attach(BasicVehicle& v, CameraMode* mode) override {  // 0x0040adf0 on [0x005f4c8c]
    program_view_attach_vehicle(v, static_cast<void*>(&v), mode);
  }
  void no_ai_controls(const char* type, const char* name) override {
    std::printf("mw3: vehicle %s (%s) has no AI controls: its own (0x004ae270) are not ported\n", name ? name : "?",
                type ? type : "?");
  }
  // A new vehicle's address: a freed object's handle forgotten (WiredBuild's on_new).
  void on_new(uint32_t, const void* p) override {
    if (g_w.forget_object) g_w.forget_object(g_w.engine, p);
  }
  Handle node_named(const char* name) override { return zcls::list_find_by_name(6, name); }  // 0x004f7fe0
  // Each vehicle its own tree, as the mechs' (models and materials copied).
  Handle node_clone(Handle node) override { return zcls::tree_copy(zn(node), 1, 1); }
  void node_set_name(Handle node, const char* name) override { zcls::node_set_name(zn(node), name); }
  void world_add(Handle node) override {
    if (zcls::Node* w = world_hooks().world) zcls::node_add_child(w, zn(node));
  }
  void world_remove(Handle node) override {
    if (zcls::Node* w = world_hooks().world) zcls::node_remove_child(w, zn(node));
  }
  void node_free(Handle node) override { zcls::tree_destroy(zn(node)); }
  void node_detach(Handle node) override { zwep::remove_handlers(node); }  // 0x00583400
  void controls_set_owner(Handle c, BasicVehicle& v) override { static_cast<VehicleControls*>(c)->owner = &v; }
  void controls_delete(Handle c) override { delete static_cast<VehicleControls*>(c); }
  // 0x004d6c90: the old node unbound (0x0040ed00), the node, its matrix and
  // markers, and the new one bound to the vehicle (0x0040eaa0: vt+0x40 and
  // vt+0xec are itself, bit 19), field bases alike.
  void entity_set_node(Vehicle& v, Handle node) override {
    void* self = static_cast<BasicVehicle*>(&v);
    if (v.node) entity_unbind(zn(v.node), self, entity_unbind_calls());
    vehicle_set_node(v, node);
    if (zn(node)) {
      EntityCasts casts;
      casts.vehicle_base = casts.vehicle = self;
      entity_bind(zn(node), self, casts);
    }
  }
  void zentity_set_node(Vehicle& v, Handle node) override { v.node = node; }            // 0x005a8390 with none
  // 0x005a7f50 / 0x005a7f70: zEntity's list, the port's (mech3/sensors).
  void entity_register(Vehicle& v) override {
    ai::entity_list().push_back(g_w.handle_of(g_w.engine, &v));
    g_vehicles.insert(&v);
    g_order.push_back(&v);
  }
  void entity_unregister(Vehicle& v) override {
    auto& l = ai::entity_list();
    l.erase(std::remove(l.begin(), l.end(), g_w.handle_of(g_w.engine, &v)), l.end());
    g_vehicles.erase(&v);
    g_order.erase(std::remove(g_order.begin(), g_order.end(), &v), g_order.end());
  }
  void anim_stop(Handle a) override { if (a) zeff::anim_stop(static_cast<zeff::AnimDef*>(a)); }  // 0x00506ca0
  // zWeapon's glow (0x00586a30, 0x00586af0) on the vehicle's node.
  int32_t glow_hit(Block398& b, Handle node, Handle weapon, float amount) override {
    return zwep::glow_hit(b, node, static_cast<zwep::WeaponDef*>(weapon), amount);
  }
  void glow_end(Block398& b, Handle node) override { zwep::glow_end(b, node); }
  void shadow_release(Vehicle&) override {}  // the AiTarget's (no shadow is made for a vehicle here)
  // 0x005a85c0: a node in the scene takes the matrix (vt+0x1c, 0x005a85a0).
  void refresh(Vehicle& v) override {
    zcls::Node* n = zn(v.node);
    if (n && n->parent_count() != 0) v.entity_moved();
  }
  void set_position(BasicVehicle& v, const float pos[3]) override {  // 0x005a8550
    zcls::object3d_set_translation(zn(v.node), pos[0], pos[1], pos[2]);
    refresh(v);
  }
  Handle marker_create(const char* name) override { return new ztwod::Image(name, 0, 0, 1); }  // 0x0059b830
  void marker_delete(Handle m) override { delete static_cast<ztwod::Image*>(m); }
  // The target view's tint (0x004fb8c0) and its switch (0x004fb840); the
  // node model's texture scroll (0x00526ae0) and its step (0x00541ad0).
  void node_tint(Handle n, const float colour[3], float amount) override {
    zcls::object3d_set_blend(static_cast<zcls::Node*>(n), colour, amount);
  }
  void node_tinted(Handle n, int32_t on) override { zcls::object3d_use_blend(static_cast<zcls::Node*>(n), on); }
  void model_scroll(Handle model, float u, float v) override {
    zmod::model_set_scroll(static_cast<zmod::Model*>(model), 1, u, v);
    zmod::model_scroll_tick(static_cast<zmod::Model*>(model));
  }
  // zWeapon's hit state and a kill (as the mechs').
  Handle hit_attacker() override { return zwep::attacker(); }
  void hit_health(float f) override { zwep::set_health_after(f); }
  void hit_killed() override { zwep::note_kill(reinterpret_cast<zwep::Handle>(1), nullptr); }
  Handle node_object(Handle node) override { return node_entity(node); }
  // obj.vt+0x80: a mech's kill credit (0x0041b420); a vehicle's 0x004a5bc0
  // does nothing.
  void credit_kill(Handle object, BasicVehicle& v) override {
    if (entity_is_mech(object)) return static_cast<Mech*>(object)->on_kill(&v);
    if (is_program_vehicle(object)) return;
    VehBasicHooks::credit_kill(object, v);
  }
  Handle anim_run(Handle def, Handle node) override {
    return def ? zeff::anim_start(static_cast<zeff::AnimDef*>(def), zn(node), 0, 0, 0) : nullptr;
  }
  void anim_start(Handle def, Handle node) override { anim_run(def, node); }
  void weapons_settle(void* w) override { static_cast<WeaponManager*>(w)->destroy_all(); }  // 0x004b1110
  const float* node_matrix(Handle node) override { return zcls::object3d_matrix(zn(node)); }
  float game_time() override { return mech_game_time(); }
  // The controls' vt+0x0c (the AI's and the base's 0x004acbc0).
  void controls_setup(Handle c, zrdr::Node* def, const char*, int32_t) override {
    static_cast<VehicleControls*>(c)->setup(def);
  }
};
WiredVehicles g_hooks;

// The controls' (veh_controls): the owner's refresh.
struct WiredControls : VehControlsHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  void owner_refresh(void* owner) override { g_hooks.refresh(*static_cast<Vehicle*>(owner)); }
};
WiredControls g_controls;

// The mission's waynet (the missions' +0xb0, [0x00609980]), app_mission's.
void* (*g_mission_waynet)() = nullptr;
void (*g_set_mission_waynet)(void*) = nullptr;

// The waynets' (mech_waynet): the "waypoint" node, the world, the nav lines,
// the mission's waynet, a mech's place and vehicle.
struct WiredWaynets : WaynetHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  Handle node_named(const char* name) override { return zcls::list_find_by_name(6, name); }
  int32_t node_parents(Handle node) override { return zn(node)->parent_count(); }
  void world_add(Handle node) override {
    if (zcls::Node* w = world_hooks().world) zcls::node_add_child(w, zn(node));
  }
  void node_set_active(Handle node, int32_t on) override { zcls::node_set_active(zn(node), on); }
  void node_set_translation(Handle node, float x, float y, float z) override {
    zcls::object3d_set_translation(zn(node), x, y, z);
  }
  Handle sound_find(const char* name) override { return zsnd::find(name); }
  void sound_play(Handle s) override { zsnd::play(static_cast<zsnd::Object*>(s), 1.0f); }
  Handle mission_waynet() override { return g_mission_waynet ? g_mission_waynet() : nullptr; }
  void set_mission_waynet(Handle w) override {  // 0x00491d00
    if (g_set_mission_waynet) g_set_mission_waynet(w);
    if (w) static_cast<Waynet*>(w)->reselect();
  }
  const float* entity_position(Handle e) override { return g_hooks.entity_position(*static_cast<Vehicle*>(e)); }
  Handle entity_vehicle(Handle e) override { return entity_is_mech(e) ? e : nullptr; }  // a mech's vt+0x50, a vehicle's 0
};
WiredWaynets g_waynets;

// The field base's (mech_fieldbase): the game type, the campaign's bases
// left, the radio, an entity's team and its controls' hit; the dock's
// clock, places, the player's view, the mech at rest and the effects. The
// scripts' loose-file path is not read here (load_motion takes motion.zbd's).
bool (*g_mp_mobile_bases)() = nullptr;
struct WiredFieldBases : FieldBaseHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  bool mp_repairs() override { return g_mp_mobile_bases ? g_mp_mobile_bases() : FieldBaseHooks::mp_repairs(); }
  void on_new(uint32_t, const void* p) override {  // as WiredBuild's
    if (g_w.forget_object) g_w.forget_object(g_w.engine, p);
  }
  int32_t game_type() override { return mission_game_type(); }
  int32_t campaign_bases() override { return program_campaign_bases(); }
  Handle campaign() override { return program_campaign(); }
  void base_lost(Handle campaign) override { field_base_cut(*static_cast<CampaignGame*>(campaign)); }
  void script_path_add(const char*, bool) override {}
  void say(Handle sound) override { sound_say(static_cast<zsnd::Object*>(sound), 1.0f); }
  float game_time() override { return mech_game_time(); }
  uint32_t frame() override { return g_hooks.frame(); }
  const float* entity_position(Handle e) override { return g_hooks.entity_position(*static_cast<Vehicle*>(e)); }
  bool powered_down(Handle e) override { return entity_is_mech(e) && mech_powered_down(e); }  // a vehicle's 0x0040bae0: 0
  // The mcbwaynet's selected point ([0x0060b544], 0x0058b060 by its +0x20),
  // its label (vt+0x24) and place (vt+0x18).
  Handle dock_point() override {
    McbWaynet* w = mcb_waynet();
    return w ? w->elem(w->current) : nullptr;
  }
  const char* dock_point_name(Handle p) override {
    const auto* pt = dynamic_cast<const WaynetPoint*>(static_cast<znet::Element*>(p));
    return pt ? pt->label_or_null() : nullptr;
  }
  const float* dock_point_position(Handle p) override {
    ai::Behaviour* b = static_cast<ai::AiElem*>(static_cast<znet::Element*>(p))->as_behaviour();
    return b ? b->position() : nullptr;
  }
  void marker_clear() override {  // 0x0047a720 on [0x0060b544]
    if (McbWaynet* w = mcb_waynet()) w->clear();
  }
  void mech_repair(Handle mech, float dt) override {  // 0x004154e0
    if (entity_is_mech(mech)) static_cast<Mech*>(mech)->repair(dt);
    else FieldBaseHooks::mech_repair(mech, dt);
  }
  Handle view_vehicle() override { return program_view_target(); }
  Handle player_vehicle() override { return mech_mission_player(); }  // the parked dock's (ours)
  int32_t view_camera() override { return program_view_camera_id(); }
  void view_set_camera(int32_t mode) override { program_view_set_camera(mode); }
  void mech_rest_at(Handle mech, const float* pos) override {
    if (entity_is_mech(mech)) static_cast<Mech*>(mech)->reset_at(pos);
    else FieldBaseHooks::mech_rest_at(mech, pos);
  }
  Handle effects_start(Handle def, Handle node) override { return g_hooks.anim_run(def, node); }
  uint32_t entity_team(Handle e) override {
    if (entity_is_mech(e) || is_program_vehicle(e)) return static_cast<Vehicle*>(e)->team;
    // Any other shooter (a turret's shot, c4m2): vt+0x28's +0xc0 as every class has it.
    if (hud_object_classed(e)) return hud_object_team(e);
    return FieldBaseHooks::entity_team(e);
  }
  void controls_hit(BasicVehicle& v, Handle weapon, uint32_t x, float amount, Handle e) override {
    if (auto* c = dynamic_cast<ai::AiVehicleControls*>(v.controls)) return c->on_hit(e);
    FieldBaseHooks::controls_hit(v, weapon, x, amount, e);
  }
};
WiredFieldBases g_field_bases;

// The field base lost's messages: the mission's message panel (0x0040fe70).
struct WiredFieldCut : FieldCutHooks {
  void message(float seconds, int32_t id, const char* text) override {
    mission_objective_hooks().hud_message(seconds, id, text);
  }
};
WiredFieldCut g_field_cut;

// zWeapon's hit handler on a vehicle's node (0x004af550 through 0x005833b0).
uint32_t vehicle_damage_handler(zwep::Handle object, zwep::WeaponDef* def, zwep::HitRecord* hit, float damage) {
  auto* v = static_cast<BasicVehicle*>(object);
  return static_cast<uint32_t>(vehicle_hit(*v, def, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit)), damage));
}
}  // namespace

namespace {
// A mech walking into a vehicle (0x004237c0, mech_collide.cpp) asks the
// entity a node belongs to. The basic vehicle's and the field base's slots:
// vt+0xec itself (0x004d2a70), vt+0x140 its controls at +0xe4 (0x004020b0)
// whose IControls vt+0x5c is the controls object (0x004a5c40, 0x004aead0,
// the AI's 0x004e7740), vt+0xf4 none for a basic vehicle (0x0040bae0, so
// always damaged) and itself for a field base, vt+0x160 the damage
// (0x004ad3f0, 0x00458f50). Anything else: none, and the census told.
BasicVehicle* hit_vehicle(Handle e) {
  if (is_program_vehicle(e)) return static_cast<BasicVehicle*>(e);
  if (e && g_w.unported) g_w.unported(g_w.engine, "0x004237c0 entity", {});
  return nullptr;
}
void wire_vehicle_collisions() {
  CollideCalls& c = collide_calls();
  c.entity_dead = [](Handle e) {  // its +0x08 clear
    const BasicVehicle* v = hit_vehicle(e);
    return v && !v->active;
  };
  c.entity_target = [](Handle e) -> Handle { return hit_vehicle(e); };
  c.entity_body = [](Handle e) -> Handle {
    BasicVehicle* v = hit_vehicle(e);
    return v ? v->controls : nullptr;
  };
  c.entity_team = [](Handle e) -> uint32_t {
    const BasicVehicle* v = hit_vehicle(e);
    return v && v->field_base ? v->team : 0;
  };
  c.entity_damage = [](Handle t, float damage) -> int32_t {  // vt+0x160(0, 0, damage): 1 killed
    BasicVehicle* v = hit_vehicle(t);
    if (!v) return 0;
    return v->field_base ? field_base_damage(*static_cast<FieldBase*>(v), nullptr, 0, damage)
                         : vehicle_damage(*v, nullptr, 0, damage);
  };
  c.body_velocity = [](Handle b) { return &static_cast<VehicleControls*>(b)->velocity.x; };  // +0x64
  c.body_spin = [](Handle b) { return &static_cast<VehicleControls*>(b)->x70.x; };           // +0x70
  c.body_mass = [](Handle b) { return static_cast<VehicleControls*>(b)->motion.inv_mass; };  // +0x9b0
  c.entity_matrix = [](Handle e) -> const float* {  // +0x0c
    const BasicVehicle* v = hit_vehicle(e);
    return v ? v->transform : nullptr;
  };
}
// The stealth object's calls out (veh_stealth.h): the ports.
struct WiredStealth : StealthHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  void on_new(uint32_t, const void* p) override {
    if (g_w.forget_object) g_w.forget_object(g_w.engine, p);
  }
  Handle object3d_create() override { return zcls::object3d_create(); }  // 0x004fb740
  zrdr::Node* reader_load(const char* file) override { return zrdr::load(file); }
  zrdr::Node* reader_find(zrdr::Node* node, const char* key) override { return node ? zrdr::find(node, key) : nullptr; }
  void reader_free(zrdr::Node* root) override {
    if (root) zrdr::free_node(root);
  }
  ai::AivRecord* aiv = nullptr;
  void aiv_open() override { aiv = new ai::AivRecord; }  // 0x004e0490
  bool aiv_named(const char* name, float pos[3], float* yaw) override {  // 0x004e0680
    ai::AivRecord* r = aiv ? aiv->read_named(name) : nullptr;
    if (!r) return false;
    for (int i = 0; i < 3; ++i) pos[i] = r->get<float>(0x08 + 4 * i);
    *yaw = r->get<float>(0x14);
    return true;
  }
  void aiv_close() override { delete aiv, aiv = nullptr; }  // 0x004e04f0
  // The overlay's readout (0x004a56b0) and its off (0x0040fb50(0)) write and
  // clear the debug lines ([0x005fc848]), never shown in the release (hud.md
  // section 20): nothing here.
  void readout(StealthLayer&) override {}
  void layer_off() override {}
  // The pick's point is read only by the editors after v1.0 (S5 on).
  void reticle_pick(ztwod::Image&) override {}
};
WiredStealth g_stealth_hooks;

// The game editor's views (game_editor.h): onto the stealth object
// ([0x00609a2c] vt+0x108, 0x004acd90); ours, at its exit, back onto the
// pilot's mech (its vt+0x108, 0x0041ce30).
struct WiredEditor : EditorHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  void view_on_stealth() override {
    if (g_stealth) vehicle_view_attach(*g_stealth);
  }
  void view_on_pilot() override {
    if (void* m = mech_mission_player()) program_view_attach(*static_cast<Mech*>(m));
  }
};
WiredEditor g_editor_hooks;
}  // namespace

void* program_stealth_controls() { return static_cast<VehicleControls*>(new StealthControls); }  // 0x004ac860(0xa1c), 0x004a51b0(1)
void* program_stealth_create(void* ctl) {
  g_stealth = stealth_create(static_cast<StealthControls*>(static_cast<VehicleControls*>(ctl)));
  return static_cast<BasicVehicle*>(g_stealth);
}
void* program_stealth() { return g_stealth ? static_cast<BasicVehicle*>(g_stealth) : nullptr; }

void wire_vehicles(const MechWiring* w) {
  if (!w) {
    g_w = MechWiring{};
    set_veh_basic_hooks(nullptr);
    set_stealth_hooks(nullptr);
    set_editor_hooks(nullptr);
    set_veh_controls_hooks(nullptr);
    set_field_base_hooks(nullptr);
    set_field_cut_hooks(nullptr);
    set_waynet_hooks(nullptr);
    ai::net_hooks().waynet_point = nullptr;
    return;
  }
  g_w = *w;
  set_veh_basic_hooks(&g_hooks);
  set_stealth_hooks(&g_stealth_hooks);
  set_editor_hooks(&g_editor_hooks);
  set_veh_controls_hooks(&g_controls);
  set_field_base_hooks(&g_field_bases);
  set_field_cut_hooks(&g_field_cut);
  set_waynet_hooks(&g_waynets);
  wire_vehicle_collisions();
  ai::net_hooks().waynet_point = waynet_point_from_node;  // 0x004bde20's
  // 0x004bdec9..0x004bdf17, while a world is loaded: the ground from 10 m
  // above the node (0x004edab0), the height picked by 0x004abd90 (a 10 m step,
  // water not ground). Unset, the nodes kept the files' heights.
  ai::net_hooks().ground = [](float p[3]) {
    float ground_pick(const zcls::GroundHits&, float, float, int32_t*, int32_t, int32_t*, float*);  // veh_basic.cpp
    zcls::Node* w = world_hooks().world;
    if (!w) return;
    zcls::GroundHits hits{};
    if (zcls::ground_point(w, p[0], p[1] + 10.0f, p[2], &hits) != 0 || hits.count == 0) return;
    int32_t best = 0, in_water = 0;
    float surface = 0;
    p[1] = ground_pick(hits, p[1], 10.0f, &best, 1, &in_water, &surface);
  };
}

// Mech3.exe 1.2 @ 0x004aeec0 (a basic vehicle's vt+0xd4): its weapons' locks
// (0x004b0370), its controls' vt+0x60 (the AI's 0x004ea700, empty), then
// AiTarget's 0x004aa4d0.
void program_vehicle_forget(void* v, uint32_t self, uint32_t x) {
  auto* b = static_cast<Vehicle*>(v);
  if (auto* w = static_cast<WeaponManager*>(b->ammo)) w->forget_target(g_w.object_of(g_w.engine, x));
  ai::forget_entity(self, x);
}

bool is_program_vehicle(const void* p) { return p && g_vehicles.count(static_cast<const Vehicle*>(p)) != 0; }
void* program_vehicle_create(void* ctl, const char* type, const char* name) { return vehicle_create(ctl, type, name); }
void* program_field_base_create(void* ctl, const char* type, const char* name) {
  return static_cast<BasicVehicle*>(field_base_create(ctl, type, name));
}
bool program_is_field_base(const void* v) {
  return is_program_vehicle(v) && is_field_base(*static_cast<const BasicVehicle*>(v));
}
int32_t program_field_bases_count() { return field_bases().count; }
DockSummary program_dock_summary() {
  const FieldBases& r = field_bases();
  DockSummary d;
  d.states = static_cast<int32_t>(r.states.size());
  d.idle_first = !r.states.empty() && r.states.front() == &r.idle;
  d.begin_next = r.begin_next;
  d.drive = field_bases_drive();
  d.deploy_valve = r.deploy_valve != nullptr;
  const DockStep* f = r.states.empty() ? nullptr : r.states.front();
  const DockStep* order[8] = {&r.idle, &r.places, &r.contact, &r.deploy, &r.repair, &r.close, &r.reset, &r.parked};
  for (int i = 0; i < 8; ++i)
    if (f == order[i]) d.front = i;
  d.busy = r.busy;
  d.requester = r.requester;
  return d;
}
int32_t program_field_bases_lines() {
  int32_t n = field_bases().effects != nullptr;
  for (Handle s : field_bases().sounds) n += s != nullptr;
  return n;
}
int32_t program_bases_alive() { return field_bases().alive(); }
bool program_mfb_ask(void* mech) { return mcb_waynet() && mfb_set_requester(mech); }  // 0x0047a780
bool program_has_mcb() { return mcb_waynet() != nullptr; }
bool program_mcb_point(float out[3]) {  // 0x0058b060 by +0x20, then vt+0x18
  McbWaynet* w = mcb_waynet();
  znet::Element* e = w ? w->elem(w->current) : nullptr;
  ai::Behaviour* b = e ? static_cast<ai::AiElem*>(e)->as_behaviour() : nullptr;
  const float* p = b ? b->position() : nullptr;
  if (!p) return false;
  out[0] = p[0], out[1] = p[1], out[2] = p[2];
  return true;
}
bool program_dock_drive() { return field_bases_drive(); }
bool program_dock_contact() { return field_bases_contact(); }
MechWear program_mech_wear(void* mech) {
  MechWear w;
  auto& m = *static_cast<Mech*>(mech);
  MechPart* const parts[5] = {m.legs[0], m.legs[1], m.arms[0], m.arms[1], m.head};
  float internal = m.torso->internal;
  for (MechPart* p : parts) w.armor += p->armor, internal = std::min(internal, p->internal);
  w.armor += m.torso->armor;
  for (int s = 1; s < 6; ++s) w.armor += m.torso->sec_armor[s];
  w.armor /= 11.0f;
  w.internal = std::min({internal, m.torso->side_internal[0], m.torso->side_internal[1]});
  auto* wm = static_cast<WeaponManager*>(m.ammo);
  std::set<AmmoBin*> bins;
  for (uint32_t i = 0; wm && i < wm->count; ++i)
    if (wm->weapons[i] && wm->weapons[i]->bin && weapon_uses_ammo(wm->weapons[i]->index)) bins.insert(wm->weapons[i]->bin);
  for (AmmoBin* b : bins) w.ammo += b->amount, w.ammo_max += b->max;
  w.coolant = m.coolant, w.jet_fuel = m.jet_fuel;
  return w;
}
void program_mech_wear_down(void* mech, float armor, float internal) {
  auto& m = *static_cast<Mech*>(mech);
  for (MechPart* p : {static_cast<MechPart*>(m.legs[0]), static_cast<MechPart*>(m.legs[1]),
                      static_cast<MechPart*>(m.arms[0]), static_cast<MechPart*>(m.arms[1]),
                      static_cast<MechPart*>(m.head), static_cast<MechPart*>(m.torso)})
    p->armor = armor, p->internal = internal;
  for (int s = 1; s < 6; ++s) m.torso->sec_armor[s] = armor;
  m.torso->side_internal[0] = m.torso->side_internal[1] = internal;
  auto* wm = static_cast<WeaponManager*>(m.ammo);
  for (uint32_t i = 0; wm && i < wm->count; ++i)
    if (wm->weapons[i] && wm->weapons[i]->bin && weapon_uses_ammo(wm->weapons[i]->index)) wm->weapons[i]->bin->amount = 0;
  m.coolant = 0, m.jet_fuel = 0;
}
void program_field_base_aim_arms(void* v, bool open) {
  if (program_is_field_base(v))
    if (FieldBaseArms* a = static_cast<FieldBase*>(v)->arms) a->aim(open);
}
void program_field_bases_mission_start() { field_bases_mission_start(); }
void program_field_bases_frame(float dt) { field_bases().frame(dt); }
void program_waynet_mission(void* (*get)(), void (*set)(void*)) { g_mission_waynet = get, g_set_mission_waynet = set; }
// 0x004d52b0: every znet object's vt+0x2c, in the registry's order. Of them
// only the waynets' (0x00479a50) changes anything in the port: the first one
// finds the marker and selects its point 0, the rest find it taken. The AI
// nets' and regions' 0x004d5350 is a no-op here (docs/ai.md, The nets' begin).
void program_nets_begin() {
  for (znet::Object* o : znet::registry().list)
    if (auto* w = dynamic_cast<Waynet*>(o)) w->begin();
}
bool program_waynet_marker(float out[3]) {
  auto* m = static_cast<zcls::Node*>(waynet_marker());
  if (!m) return false;
  zcls::object3d_get_translation(m, &out[0], &out[1], &out[2]);
  return true;
}
bool program_waynet_selected(int32_t i, float out[3]) {
  for (znet::Object* o : znet::registry().list)
    if (auto* w = dynamic_cast<Waynet*>(o); w && i-- == 0) {
      const float* p = w->position_of(w->current);
      if (!p) return false;
      out[0] = p[0], out[1] = p[1], out[2] = p[2];
      return true;
    }
  return false;
}
int32_t program_waynets(const char** names, int32_t* points, int32_t cap) {  // for the tests
  int32_t n = 0;
  for (znet::Object* o : znet::registry().list)
    if (auto* w = dynamic_cast<Waynet*>(o)) {
      if (n < cap) names[n] = w->label.c_str(), points[n] = w->count() - 1;
      ++n;
    }
  return n;
}
void program_mcb_command(void* mech) {  // 0x0047a910 (MCB_COMMAND)
  if (McbWaynet* w = mcb_waynet()) w->command(mech);
}
bool program_is_nav_waynet(void* o);
bool program_mission_waynet_key(int32_t cmd, void* mech) {  // [0x00609980] vt+0x60 / +0x64 / +0x68
  auto* w = static_cast<Waynet*>(g_mission_waynet ? g_mission_waynet() : nullptr);
  if (!w || (w != mcb_waynet() && !program_is_nav_waynet(w))) return false;  // the dock's or the nav points
  if (cmd == 0x36) w->next();
  else if (cmd == 0x37) w->prev();
  else w->nearest(g_hooks.entity_position(*static_cast<Vehicle*>(mech)));
  return true;
}
void program_mission_waynet_clear() {  // 0x0047a720 on [0x0060b544], the mission's cleanup
  if (McbWaynet* w = mcb_waynet()) w->clear();
}
void program_mcb_waynet_free() { mcb_waynet_free(); }  // 0x004d54f0
void* program_mcb_waynet() { return mcb_waynet(); }
int32_t program_mcb_points() { return mcb_waynet() ? mcb_waynet()->count() - 1 : -1; }
void* program_field_base(int32_t i) {
  return i >= 0 && i < 3 ? static_cast<BasicVehicle*>(field_bases().bases[i]) : nullptr;
}
bool program_mcb_point(int32_t i, float pos[3]) {
  const Waynet* w = mcb_waynet();
  if (!w || i < 1 || i >= w->count()) return false;
  const float* p = w->position_of(i);
  pos[0] = p[0], pos[1] = p[1], pos[2] = p[2];
  return true;
}
bool program_dock_point(const char** label, float pos[3]) {
  const FieldBases& r = field_bases();
  const auto* pt = dynamic_cast<const WaynetPoint*>(static_cast<znet::Element*>(r.dock_point));
  if (!pt) return false;
  if (label) *label = pt->label_or_null();
  pos[0] = pt->pos[0], pos[1] = pt->pos[1], pos[2] = pt->pos[2];
  return true;
}
void* program_mission_waynet() { return g_mission_waynet ? g_mission_waynet() : nullptr; }
namespace {
std::set<Waynet*> g_navs;  // the nav waynets 0x004797c0 made (the missions' +0xb4)
}  // namespace
void* program_nav_waynet_make(const char* name) {
  Waynet* w = nav_waynet_make(name);
  if (w) g_navs.insert(w);
  return w;
}
bool program_nav_waynet_delete(void* o) {
  auto* w = static_cast<Waynet*>(o);
  if (!g_navs.erase(w)) return false;
  delete w;  // vt+0x00(1): 0x004796f0, the icons counted out
  return true;
}
bool program_is_nav_waynet(void* o) { return g_navs.count(static_cast<Waynet*>(o)) != 0; }
uint8_t program_nav_waynet_frame(void* w, void* vehicle) { return static_cast<Waynet*>(w)->frame(vehicle); }
void program_waynet_icons_free() { waynet_icons_free(); }
void program_set_mp_mobile_bases(bool (*fn)()) { g_mp_mobile_bases = fn; }

bool program_mission_point(int32_t i, const char** label, float pos[3]) {
  auto* w = static_cast<Waynet*>(program_mission_waynet());
  WaynetPoint* p = w && i >= 0 && i < w->count() ? as_point(w->elem(i)) : nullptr;
  if (!p || !p->position()) return false;
  *label = p->label_or_null();
  for (int k = 0; k < 3; ++k) pos[k] = p->position()[k];
  return true;
}

// Mech3.exe 1.2 @ 0x00490950: at each point of the mission's waynet a copy of
// the world's mfb_station01 (0x004f9af0(.., 0, 0)), added to the world,
// placed there, named mfb_station_04 on and shown; the point's label (+0x2c)
// the station's name, which the dock asks by (mech_fieldbase). Every element
// of a built waynet is a point with a place (the emulator on the shipped
// waynet_mp files); one without would be passed over (the exe would read
// through null).
int32_t program_mp_stations(void* waynet) {
  auto* w = static_cast<Waynet*>(waynet);
  if (!w) return 0;
  zcls::Node* station = static_cast<zcls::Node*>(zcls::list_find_by_name(6, "mfb_station01"));
  if (!station) return 0;
  int32_t n = 4, made = 0;
  for (int32_t i = 0; i < w->count(); ++i) {
    auto* b = dynamic_cast<ai::Behaviour*>(w->elem(i));
    const float* at = b ? b->position() : nullptr;  // vt+0x18
    if (!at) continue;
    zcls::Node* copy = zcls::tree_copy(station, 0, 0);
    if (!copy) continue;
    char name[32];
    std::snprintf(name, sizeof name, "mfb_station_%02d", n++);
    if (zcls::Node* world = world_hooks().world) zcls::node_add_child(world, copy);  // [0x00608c84] vt+0
    zcls::object3d_set_translation(copy, at[0], at[1], at[2]);
    zcls::node_set_name(copy, name);
    zcls::node_set_active(copy, 1);
    if (WaynetPoint* p = as_point(w->elem(i))) p->label = name, p->has_label = true;
    ++made;
  }
  return made;
}

// The mission waynet's selected element for the radar: [0x00609980]'s
// 0x0058b060 by its +0x20. FIX (bug): only a point with a place; the exe
// reads the default behaviour's +0x0c and its null place (the MFB picking's
// N / Shift+N wraps onto it).
void* program_waypoint_marker() {
  auto* w = static_cast<Waynet*>(program_mission_waynet());
  WaynetPoint* p = w ? as_point(w->elem(w->current)) : nullptr;
  return p && p->position() ? p : nullptr;
}
bool program_is_waypoint_icon(const void* b) { return b && (b == waynet_icon(false) || b == waynet_icon(true)); }
void* program_waypoint_icon(void* p) { return waynet_icon(static_cast<WaynetPoint*>(p)->mfb != 0); }
void program_waypoint_position(void* p, float out[3]) {
  const float* q = static_cast<WaynetPoint*>(p)->position();
  out[0] = q[0], out[1] = q[1], out[2] = q[2];
}
const char* program_waypoint_name(void* p) {
  const auto* pt = static_cast<WaynetPoint*>(p);
  return pt->has_name ? pt->name.c_str() : nullptr;
}
NavSummary program_nav_summary(void* waynet) {
  NavSummary n;
  auto* w = static_cast<Waynet*>(waynet);
  if (!w) return n;
  n.count = w->count();
  n.current = w->current;
  n.watched = static_cast<int32_t>(w->watched.size());
  for (int32_t i = 0; i < w->count() && i < 8; ++i)
    if (const WaynetPoint* p = as_point(w->elem(i))) {
      n.labels[i] = p->label_or_null() ? p->label : "";
      n.names[i] = p->has_name ? p->name : "";
      n.reached[i] = p->reached != 0;
      n.has_line[i] = p->sound != nullptr;
      n.pos[i][0] = p->pos[0], n.pos[i][1] = p->pos[1], n.pos[i][2] = p->pos[2];
    }
  return n;
}
void* program_nav_find(const char* name) {  // 0x00479be0 on [0x00609980]
  auto* w = static_cast<Waynet*>(program_mission_waynet());
  return w ? w->find(name) : nullptr;
}
const char* program_nav_name(void* p) { return p ? static_cast<WaynetPoint*>(p)->name.c_str() : ""; }
void program_nav_watch(void* p, int32_t on) {  // 0x0047a270 on [0x00609980]
  if (auto* w = static_cast<Waynet*>(program_mission_waynet())) w->note(static_cast<WaynetPoint*>(p), on != 0);
}
void program_waypoint_arrow(int32_t active) {  // 0x00479ef0 on [0x00609980]
  if (auto* w = static_cast<Waynet*>(program_mission_waynet())) w->hud_arrow(active != 0);
}
void program_waypoint_arrow_draw() {  // 0x00479ed0 while [0x00609980]
  if (program_mission_waynet()) waynet_arrow_draw();
}
void* program_vehicle_controls(void* v) { return static_cast<BasicVehicle*>(v)->controls; }
void* program_vehicle_node(void* v) { return static_cast<BasicVehicle*>(v)->node; }
void* program_vehicle_centroid(void* v) { return static_cast<BasicVehicle*>(v)->centroid; }
void* program_vehicle_target_node(void* v) { return static_cast<BasicVehicle*>(v)->target_node; }
void program_vehicle_view_tint(void* v, bool on) { vehicle_view_tint(*static_cast<BasicVehicle*>(v), on ? 1 : 0); }
void* program_vehicle_marker(int32_t kind) {
  const VehicleMarkers& m = vehicle_markers();
  return kind == 0 ? m.ally : kind == 1 ? m.enemy : kind == 2 ? m.other : nullptr;
}
void program_vehicle_place(void* v, const float pos[3], float yaw) { vehicle_place(*static_cast<BasicVehicle*>(v), pos, yaw); }
void program_vehicle_setup(void* v, void* def, const char* name, int32_t kind) {
  auto* b = static_cast<BasicVehicle*>(v);
  if (b->field_base) return field_base_setup(*static_cast<FieldBase*>(b), static_cast<zrdr::Node*>(def), name, kind);
  vehicle_setup(*b, static_cast<zrdr::Node*>(def), name, kind);
}
void program_vehicle_motion(void* v, void* mode, const char* name, const char* type) {
  auto* b = static_cast<BasicVehicle*>(v);
  if (!b->controls) return;
  vehicle_motion_read(*b->controls, b->node, static_cast<zrdr::Node*>(mode), name, type, &b->controls->point_count);
}
void program_vehicle_points(void* v) { vehicle_points_place(*static_cast<BasicVehicle*>(v)); }
void program_vehicle_touch(void* v) {
  auto* b = static_cast<BasicVehicle*>(v);
  zwep::set_damage_handler(b, b->node, vehicle_damage_handler, 1);
}
void program_vehicle_path_drop(void* v) {
  if (is_program_vehicle(v)) veh_basic_hooks().path_drop_temporary(*static_cast<BasicVehicle*>(v));
}
void program_vehicle_refresh(void* v) { g_hooks.refresh(*static_cast<BasicVehicle*>(v)); }
bool program_vehicle_mask(void* v, uint32_t off, uint32_t* get, const uint32_t* set) {
  auto* b = static_cast<Vehicle*>(v);
  uint32_t* f = off == 0xc0 ? &b->team : off == 0xc4 ? &b->allies : off == 0xc8 ? &b->enemies : nullptr;
  if (!f) return false;
  if (set) *f = *set;
  if (get) *get = *f;
  return true;
}
namespace {
BasicVehicle& bv(void* v) { return *static_cast<BasicVehicle*>(v); }
}  // namespace
float program_vehicle_health(void* v) { return vehicle_health_share(bv(v)); }
float program_vehicle_speed_at(void* v, float share) { return vehicle_speed_at(bv(v), share); }
float program_vehicle_stopping_distance(void* v, float s) { return vehicle_stopping_distance(bv(v), s); }
float program_vehicle_stop_share(void* v, float d) { return vehicle_stop_share(bv(v), d); }
float program_vehicle_move_speed(void* v) { return vehicle_move_speed(bv(v)); }
bool program_vehicle_can_face(void* v, const float d[3]) { return vehicle_gun_within_world(bv(v), d); }
bool program_vehicle_in_reach(void* v, float cosine) { return vehicle_can_aim(bv(v), cosine); }
bool program_vehicle_aim_clamp(void* v, const float d[3], float out[3]) { return vehicle_gun_clamp(bv(v), d, out); }
void program_vehicle_set_aim(void* v, const float d[3]) { vehicle_aim_at(bv(v), d, 0); }
void program_vehicle_velocity(void* v, float out[3]) {
  const Vec3& u = bv(v).velocity;
  out[0] = u.x, out[1] = u.y, out[2] = u.z;
}
const float* program_vehicle_velocity_at(void* v) { return &bv(v).velocity.x; }
const char* program_vehicle_name(void* v) {
  const auto* n = static_cast<const zcls::Node*>(bv(v).node);
  return n ? n->name() : nullptr;
}
void* program_vehicle_target(void* v) { return static_cast<ai::AiTarget*>(&bv(v)); }
void program_vehicle_hit_an_ally(void* v, void* victim) {
  if (auto* c = dynamic_cast<ai::AiVehicleControls*>(bv(v).controls)) c->hit_an_ally(victim);
}
// The stealth object's frame from the player's tick while the view is on it
// (0x004a1160: vt+0x34, 0x004a5050, called straight, not through the entity
// walk's +0x09 test).
void program_stealth_frame(float dt) {
  if (g_stealth) stealth_frame(*g_stealth, dt);
}
// Mech3.exe 1.2 @ 0x004a50d0 (the stealth's vt+0x104): its overlay off.
void program_vehicle_view_detached(Vehicle& v) {
  if (g_stealth && &v == static_cast<Vehicle*>(g_stealth) && g_stealth->layer) g_stealth->layer->set_active(0);
}
void program_vehicle_frame(void* v, float dt) {
  if (!bv(v).x09) return;
  if (bv(v).field_base) return field_base_frame(*static_cast<FieldBase*>(v), dt);
  vehicle_frame(bv(v), dt);
}
namespace {
ai::Brain* brain_of(void* v) {
  auto* c = dynamic_cast<ai::AiVehicleControls*>(bv(v).controls);
  return c ? c->brain.get() : nullptr;
}
}  // namespace
bool program_vehicle_awake(void* v) {
  if (!bv(v).active) return false;
  const ai::Brain* b = brain_of(v);
  return !b || b->p6_clear();
}
bool program_vehicle_sees(void* v, uint32_t self) {
  ai::Brain* b = brain_of(v);
  return !b || b->sees_target(self);
}
void program_vehicle_set_sensed(void* v, bool on) { bv(v).x09 = on; }
bool program_vehicle_sensed(const void* v) { return static_cast<const Vehicle*>(v)->x09; }
void program_vehicle_sense_refresh(void* v) { bv(v).engine.release(); }
void program_vehicle_engine_play(void* v, const float pos[3]) { bv(v).engine.play(pos, nullptr, 0); }
uint8_t program_vehicle_active(const void* p) { return static_cast<const Vehicle*>(p)->active ? 1 : 0; }
void* program_vehicle_named(const char* name) {
  for (Vehicle* v : g_order) {
    const auto* n = static_cast<const zcls::Node*>(v->node);
    if (name && n && n->name() && std::strcmp(n->name(), name) == 0) return v;
  }
  return nullptr;
}
void program_vehicle_destroy(void* v) { g_hooks.die(bv(v)); }  // vt+0x70 by the class
void* program_vehicle_brain(void* controls) {
  for (Vehicle* v : g_order)
    if (static_cast<BasicVehicle*>(v)->controls == controls)
      if (auto* c = dynamic_cast<ai::AiVehicleControls*>(static_cast<BasicVehicle*>(v)->controls)) return c->brain.get();
  return nullptr;
}
bool program_vehicle_kill(void* v, void* killer, const char* weapon) {
  zwep::WeaponDef* def = zwep::find_def(weapon);
  if (!is_program_vehicle(v) || !def || !bv(v).controls) return false;
  zwep::hit_state().attacker = killer ? static_cast<Vehicle*>(killer)->node : nullptr;
  g_hooks.damage(bv(v), def, 0, bv(v).controls->health + 1.0f);  // vt+0x160 by the class
  zwep::hit_state().attacker = nullptr;
  return !bv(v).active;
}
float program_vehicle_hit_by(void* v, const char* weapon, float amount) {
  zwep::WeaponDef* def = zwep::find_def(weapon);
  if (!is_program_vehicle(v) || !def || !bv(v).controls) return 0.0f;
  g_hooks.damage(bv(v), def, 0, amount);
  return bv(v).controls->health / bv(v).controls->health_max;
}
void* program_vehicle_glow_light(const void* v) {
  if (!is_program_vehicle(v)) return nullptr;
  const VehicleControls* c = static_cast<const BasicVehicle*>(v)->controls;
  return c ? c->x398.light : nullptr;
}
float program_vehicle_damage(void* v, float amount) {
  if (!is_program_vehicle(v) || !bv(v).controls) return 0.0f;
  g_hooks.damage(bv(v), nullptr, 0, amount);  // vt+0x160 by the class, no weapon
  return bv(v).controls->health / bv(v).controls->health_max;
}
bool program_vehicle_free(void* v) {
  if (v && v == program_stealth()) {  // 0x004a4fa0(1); made before the mission's vehicles, not among them
    stealth_delete(g_stealth), g_stealth = nullptr;
    g_vehicles.erase(static_cast<Vehicle*>(v));
    g_order.erase(std::remove(g_order.begin(), g_order.end(), static_cast<Vehicle*>(v)), g_order.end());
    if (g_w.forget_object) g_w.forget_object(g_w.engine, v);
    return true;
  }
  if (!is_program_vehicle(v)) return false;
  // FIX (bug): a vehicle still glowing (alive at the mission's end) gives
  // zWeapon's light back first; the exe returns it only at the death
  // (0x004ad36d), and zClass then refuses to free it with the world
  // (0x004fa850: still in the world's light list). fixes.md.
  if (VehicleControls* c = bv(v).controls; c && c->x398.light) zwep::glow_end(c->x398, c->node);
  if (bv(v).field_base) field_base_delete(static_cast<FieldBase*>(v));  // vt+0x00 0x00458b30(1)
  else vehicle_delete(static_cast<BasicVehicle*>(v));                   // 0x004ac630(1)
  g_vehicles.erase(static_cast<Vehicle*>(v));     // its zEntity entry goes in the destructor
  g_order.erase(std::remove(g_order.begin(), g_order.end(), static_cast<Vehicle*>(v)), g_order.end());
  if (g_w.forget_object) g_w.forget_object(g_w.engine, v);
  return true;
}
void program_vehicles_new_process() {  // tests: stop_game freed the stealth's node with the rest
  g_stealth = nullptr;
  g_vehicles.clear(), g_order.clear();
}
void program_vehicles_forget() {
  g_vehicles.clear(), g_order.clear();
  if (g_stealth) {  // made by the world's load, before the mission's vehicles: it stays the world's
    g_vehicles.insert(static_cast<Vehicle*>(static_cast<BasicVehicle*>(g_stealth)));
    g_order.push_back(static_cast<Vehicle*>(static_cast<BasicVehicle*>(g_stealth)));
  }
}
std::vector<void*> program_vehicles() {  // the mission's: the world's stealth object apart
  std::vector<void*> out;
  for (Vehicle* v : g_order)
    if (!g_stealth || v != static_cast<Vehicle*>(static_cast<BasicVehicle*>(g_stealth))) out.push_back(v);
  return out;
}
bool describe_vehicle(const void* p, VehicleSummary* out) {
  if (!is_program_vehicle(p)) return false;
  const auto* v = static_cast<const BasicVehicle*>(p);
  const auto* n = static_cast<const zcls::Node*>(v->node);
  *out = VehicleSummary{};
  out->name = n ? n->name() : nullptr;
  out->in_world = n && n->parent_count() > 0;
  out->active = v->active;
  out->team = v->team;
  if (v->transform) out->pos[0] = v->transform[9], out->pos[1] = v->transform[10], out->pos[2] = v->transform[11];
  if (const VehicleControls* c = v->controls) {
    out->ai_controls = dynamic_cast<const ai::AiVehicleControls*>(c) != nullptr;
    out->owned = c->owner == v;
    out->health = c->health, out->health_max = c->health_max;
    out->mode = c->motion.mode, out->supports = c->motion.support_count, out->points = c->point_count;
  }
  if (v->field_base) {
    const auto* b = static_cast<const FieldBase*>(v);
    out->field_base = true;
    for (const zsis::Part* p : b->parts) out->arm_parts += p != nullptr;
    out->arms = b->arms ? b->arms->now : 0;
    for (int i = 0; i < field_bases().count && i < 3; ++i)
      if (field_bases().bases[i] == b) out->registry = i;
  }
  return true;
}

}  // namespace mech3
