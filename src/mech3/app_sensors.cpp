// The sensors' hooks wired to the program (docs/ai.md section 15): the
// virtual calls the exe makes on vehicles answered as the Mech's and
// CZAiTarget's vtables do (0x005bcef8, 0x005ccb90), their fields read from
// the ports' objects, the nodes' world positions from zClass. Not original
// code. What the mech's slots do beyond the sensors (the HUD's lock 0x0041d890,
// the brain's refresh 0x004aedd0, empty) is noted; the level of detail is mech_lod's.
// The animation-owner registry's hooks too (ai.md section 18).
#include "mech3/app_sensors.h"
#include "mech3/app_hud.h"
#include "mech3/app_mech.h"
#include "mech3/app_turret.h"
#include "mech3/app_vehicle.h"
#include "mech3/mech_hud.h"
#include "mech3/mech_lod.h"
#include "mech3/mech_parts.h"
#include "mech3/pocket_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/weapon.h"
#include "mech3/mech.h"
#include "mech3/mech_control.h"
#include "mech3/sensors.h"
#include "mech3/entity_classes.h"
#include "mech3/turret.h"
#include "mech3/zai_anim_owner.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_target.h"
#include "mech3/zai_valve.h"
#include "mech3/zainet_inout.h"
#include "mech3/zainet_region.h"
#include "mech3/zrdrainet.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"
#include "mech3/mission_world.h"

namespace mech3 {

void player_set_60b56c(uint8_t v);  // mech3/player.h, whose Handle is not mech.h's
const char* program_pilot_name();  // app_menus.cpp: the current pilot's (+0x18)

namespace {

MechWiring g_w;
const char* (*g_message)(void* e, int32_t id) = nullptr;

uint32_t log_unported(const char* key, std::initializer_list<zgame::Arg> args) {
  return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
}
uint32_t handle(const void* p) { return p && g_w.handle_of ? g_w.handle_of(g_w.engine, p) : 0; }
void* object(uint32_t h) { return h && g_w.object_of ? g_w.object_of(g_w.engine, h) : nullptr; }

// The mechs are zEntity's entries that are mechs (entity_is_mech, the exe's
// vt+0x2c then vt+0x50); the targets are targets.zrd's.
Mech* mech(uint32_t h) {
  void* o = object(h);
  return entity_is_mech(o) ? static_cast<Mech*>(o) : nullptr;
}
// The basic vehicles (app_vehicle.cpp).
void* vehicle(uint32_t h) {
  void* o = object(h);
  return is_program_vehicle(o) ? o : nullptr;
}
ai::CZAiTarget* target(uint32_t h) {
  for (ai::CZAiTarget* t : ai::target_list())
    if (h && t->self == h) return t;
  return nullptr;
}
ai::Brain* brain_of(const Mech* m) {
  auto* c = m ? dynamic_cast<AiControls*>(m->controls) : nullptr;
  return c ? c->brain.get() : nullptr;
}

// 0x00448600 on a node: its world matrix refreshed when stale (0x0051e9d0,
// 0x0051fde0, 0x004ed280(node, 1), 0x0051ea00), the translation (+0x84).
void world_position(zcls::Node* n, float out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (!n || !n->data || n->cls() != 5) return;
  auto* d = static_cast<zcls::Object3d*>(n->data);
  if (d->get<uint32_t>(0) & 0x20) {
    zmth::Mat34 buf;
    zmth::push(&buf);
    zmth::load_identity();
    zcls::node_to_world(n, 1);
    zmth::pop();
  }
  for (int i = 0; i < 3; ++i) out[i] = d->floats(0x84)[i];
}
// The node 0x005a8630 reads: the object's target node (zEntity +0x10); a
// turret's or target's own node when it has none (as entity_lock_position).
zcls::Node* aim_node(zgame::Handle h) {
  if (Mech* m = mech(h)) return static_cast<zcls::Node*>(m->target_node);
  if (void* v = vehicle(h)) return static_cast<zcls::Node*>(program_vehicle_target_node(v));
  zgame::Handle n = 0;
  if (tur::Turret* t = program_turret_by_handle(h)) n = t->target_node ? t->target_node : t->node;
  else if (ai::CZAiTarget* t = target(h)) n = t->target_node ? t->target_node : t->node;
  return static_cast<zcls::Node*>(object(n));
}

struct ProgramSensors : ai::SensorHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return log_unported(key, args);
  }
  uint32_t vcall(zgame::Handle o, uint32_t slot, std::initializer_list<zgame::Arg> args) override {
    if (Mech* m = mech(o)) {  // 0x005bcef8
      switch (slot) {
        case 0x24: case 0x28: case 0x2c: case 0x40: case 0x50: return o;  // 0x004d2a70: itself
        case 0xa0: return m->power_down_pending || m->powered_down ? 1 : 0;  // 0x0041cc10
        case 0xb4: case 0xec: return 0;
        case 0xcc: return m->active ? 1 : 0;  // 0x004aee00: +0x08, the controls' answer
        case 0xd0: return static_cast<uint32_t>(lod_update(*m));  // 0x0041e870: the level of detail
        case 0xc8:  // 0x0041e810: the brain told, then the detail budget
          // 0x004aedd0: the controls' (vt+0x140) brain (vt+0x54) vt+0x08, the
          // empty 0x005ac5c0 in every brain's vtable (ai.md, The brain's
          // refresh): nothing to do.
          if (Mech* p = world().mission_player) lod_budget(*p);
          return 0;
        case 0x140: return handle(m->controls);
        case 0x1c: log_unported("0x005a85a0", {o}); return 0;
        default: break;
      }
    } else if (void* v = vehicle(o)) {  // the basic vehicle's 0x005c88e8
      switch (slot) {
        case 0x24: case 0x28: case 0x2c: case 0x40: case 0xec: return o;  // 0x004d2a70
        case 0x50: case 0xa0: case 0xb4: return 0;  // 0x0040bae0, 0x004d7290
        case 0xcc: return program_vehicle_awake(v) ? 1 : 0;   // 0x004aee00
        case 0xd0: return program_vehicle_sees(v, o) ? 1 : 0; // 0x004aee40
        case 0xc8: program_vehicle_sense_refresh(v); return 0;  // 0x004add90
        case 0x140: return handle(program_vehicle_controls(v));  // 0x004020b0
        case 0x1c: program_vehicle_refresh(v); return 0;      // 0x005a85a0 as 0x005a85c0's
        default: break;
      }
    } else if (tur::Turret* t = program_turret_by_handle(o)) {  // 0x005c8770
      switch (slot) {
        case 0x20: case 0x24: case 0x28: case 0x2c: case 0x48: case 0x4c: return o;  // 0x004d2a70
        case 0xa0: return t->idle_now() ? 1 : 0;  // 0x004a7a60
        case 0xcc: return t->awake() ? 1 : 0;     // 0x004a7a70
        case 0xd0: return t->in_range() ? 1 : 0;  // 0x004a7ab0
        default: return 0;  // 0x0040bae0, 0x004d7290, the empty 0x005ac5c0 (vt+0xc8)
      }
    } else if (ai::CZAiTarget* t = target(o)) {  // 0x005ccb90
      switch (slot) {
        case 0x24: case 0x28: case 0x2c: return o;
        case 0xb4: return (t->desc.flags & 2) ? 0 : 1;  // 0x004d3380: hidden unless "radar"
        case 0xcc: return t->active;                    // 0x004d7200
        case 0xd0: return 1;                            // 0x004d7210
        default: return 0;
      }
    } else if (const PocketMarker* k = program_pocket_marker(o)) {  // a plain AiTarget, 0x005ccfe0
      switch (slot) {
        case 0x24: case 0x28: case 0x2c: return o;    // 0x004d2a70
        case 0x40: case 0x50: case 0xa0: return 0;    // 0x0040bae0
        case 0xb4: return 0;                          // 0x004d7290
        case 0xc8: return 0;                          // the empty 0x005ac5c0
        case 0xcc: return k->active;                  // 0x004d7200: +0x08
        case 0xd0: return 1;                          // 0x004d7210
        default: break;  // vt+0x1c (0x005a85a0) logged; +0xec and on are past its vtable
      }
    } else if (slot == 0x54) {  // the controls' brain (IControls vt+0x54)
      // By the AI mechs made, not zEntity's list: the entity sort asks this
      // while that list is mid-sort, an entry moved out of it.
      int32_t n = 0;
      const AiMechMade* made = mission_ai_mechs(&n);
      for (int32_t i = 0; i < n; ++i) {
        Mech* m = static_cast<Mech*>(made[i].mech);
        if (m && handle(m->controls) == o) return handle(brain_of(m));
      }
      if (void* b = program_vehicle_brain(object(o))) return handle(b);
      return 0;
    }
    return SensorHooks::vcall(o, slot, args);
  }
  ai::AiTarget* fields(zgame::Handle x) override {
    if (Mech* m = mech(x)) return m;
    if (void* v = vehicle(x)) return static_cast<ai::AiTarget*>(program_vehicle_target(v));
    if (tur::Turret* t = program_turret_by_handle(x)) return t;
    return target(x);
  }
  int32_t equipment(zgame::Handle m, int32_t kind) override {
    const Mech* p = mech(m);
    return p && kind >= 0 && kind < 15 ? p->equipment[kind] : 0;
  }
  uint8_t active(zgame::Handle t) override {
    const ai::CZAiTarget* p = target(t);
    return p ? p->active : 0;
  }
  void set_sensed(zgame::Handle e, uint8_t on) override {
    if (Mech* m = mech(e)) m->x09 = on != 0;
    else if (void* v = vehicle(e)) program_vehicle_set_sensed(v, on != 0);
    else if (tur::Turret* t = program_turret_by_handle(e)) t->sensed = on;
    else if (ai::CZAiTarget* t = target(e)) t->sensed = on;
  }
  zcls::Node* node(zgame::Handle x, bool centroid) {
    if (Mech* m = mech(x)) return static_cast<zcls::Node*>(centroid ? m->centroid : m->node);
    if (void* v = vehicle(x))
      return static_cast<zcls::Node*>(centroid ? program_vehicle_centroid(v) : program_vehicle_node(v));
    if (tur::Turret* t = program_turret_by_handle(x))
      return static_cast<zcls::Node*>(object(centroid ? t->centroid_node : t->node));
    if (ai::CZAiTarget* t = target(x)) return static_cast<zcls::Node*>(object(centroid ? t->centroid_node : t->node));
    return nullptr;
  }
  void centroid(zgame::Handle x, float out[3]) override { world_position(node(x, true), out); }
  void node_position(zgame::Handle x, float out[3]) override { world_position(node(x, false), out); }
  void entity_refresh(zgame::Handle x) override {
    if (Mech* m = mech(x)) m->entity_moved();  // 0x005a85c0: its vt+0x1c, as the mech's own update calls it
    else if (void* v = vehicle(x)) program_vehicle_refresh(v);
  }
  // 0x004adce0 on the pinged vehicle's engine sound (vt+0xec is the vehicle).
  void ping(zgame::Handle s, const float pos[3]) override {
    if (void* v = vehicle(s)) return program_vehicle_engine_play(v, pos);
    SensorHooks::ping(s, pos);
  }
  // vt+0xd4 by the class: a mech's and a basic vehicle's 0x004aeec0, the
  // rest (turrets, the command marker) AiTarget's 0x004aa4d0.
  void forget(zgame::Handle v, zgame::Handle x) override {
    if (Mech* m = mech(v)) return program_mech_forget(m, v, x);
    if (void* b = vehicle(v)) return program_vehicle_forget(b, v, x);
    ai::forget_entity(v, x);
  }
  void anim_owner_release(zgame::Handle x) override { ai::anim_owner_release(x); }
  void set_target(zgame::Handle x, ai::Contact* c, bool keep) override {
    ai::AiTarget* a = fields(x);
    if (!a) return;
    a->set_target(c, keep);
    if (!keep && mech(x) && g_w.view && x == g_w.peek32(g_w.engine, g_w.view(g_w.engine) + 0x64))
      program_hud_target_set(mech(x));  // 0x0041d890's HUD part: TARGET on for the view mech's new target
  }
  uint8_t brain_lancemate(zgame::Handle b) override {
    const auto* p = static_cast<const ai::Brain*>(object(b));
    return p ? p->params.raw[0xcd] : 0;
  }
  const char* brain_pilot(zgame::Handle b) override {
    const auto* p = static_cast<const ai::Brain*>(object(b));
    return p ? p->params.pilot : nullptr;
  }
  const char* message(int32_t id) override { return g_message ? g_message(g_w.engine, id) : nullptr; }
  zgame::Handle player() override {
    const uint32_t p = g_w.pilot ? g_w.pilot(g_w.engine) : 0;
    return p ? g_w.peek32(g_w.engine, p + 0x14) : 0;
  }
  zgame::Handle view_target() override {
    const uint32_t v = g_w.view ? g_w.view(g_w.engine) : 0;
    return v ? g_w.peek32(g_w.engine, v + 0x64) : 0;
  }
  zgame::Handle stealth() override { return g_w.peek32 ? g_w.peek32(g_w.engine, 0x609a2c) : 0; }
  void set_sorted() override { player_set_60b56c(1); }  // [0x0060b56c]
};
ProgramSensors g_sensors;

// 0x004d4b10, the animations' callback the registry sets.
void owner_callback(zeff::AnimDef* d, void* rec, int32_t msg) {
  ai::anim_owner_message(handle(d), static_cast<ai::AnimOwnerRec*>(rec), msg);
}

// The animation-owner registry's hooks (zai_anim_owner.h): zEffect's
// animations, each class's messages (vt+0x74) and placement (vt+0xdc).
struct ProgramAnimOwner : ai::AnimOwnerHooks {
  float place_[3] = {};
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return log_unported(key, args);
  }
  static zeff::AnimDef* def(zgame::Handle a) { return static_cast<zeff::AnimDef*>(object(a)); }
  ai::AnimCallback anim_callback(zgame::Handle a) override {
    const zeff::AnimDef* d = def(a);
    if (!d || !d->callback) return ai::AnimCallback::none;
    return d->callback == &owner_callback ? ai::AnimCallback::ours : ai::AnimCallback::other;
  }
  ai::AnimOwnerRec* anim_record(zgame::Handle a) override {
    const zeff::AnimDef* d = def(a);
    return d ? static_cast<ai::AnimOwnerRec*>(d->callback_arg) : nullptr;
  }
  void anim_set_record(zgame::Handle a, ai::AnimOwnerRec* r) override { zeff::anim_set_callback(def(a), &owner_callback, r); }
  // vt+0x74: a target's 0x004d7270 (0 and 0x38), a turret's 0x004a74f0; the
  // mechs', vehicles' and field bases' 0x004d7270 reaches empty slots.
  void entity_message(zgame::Handle o, zgame::Handle a, int32_t msg) override {
    if (tur::Turret* t = program_turret_by_handle(o)) return t->message(a, msg);
    if (ai::CZAiTarget* t = target(o)) t->message(msg);
  }
  void valve_set(zgame::Handle v, int32_t x) override {
    if (auto* valve = static_cast<ai::Valve*>(object(v))) valve->set(x);
  }
  // 0x004c3bd0: the animation's node (0x0050b5e0, none once freed) names the polygons.
  void inout_message(zgame::Handle a, int32_t msg) override {
    const zeff::AnimDef* d = def(a);
    const zcls::Node* n = d && d->raw[0x98] != 5 ? d->node : nullptr;
    ai::inout_set_state(n ? n->name() : nullptr, msg);
  }
  zgame::Handle player_vehicle() override { return g_sensors.player(); }
  zgame::Handle node_named(const char* name) override { return handle(zcls::list_find_by_name(6, name)); }
  void node_translation(zgame::Handle n, float out[3]) override {
    zcls::object3d_get_translation(static_cast<zcls::Node*>(object(n)), &out[0], &out[1], &out[2]);
  }
  void node_rotation(zgame::Handle n, float out[3]) override {
    zcls::object3d_get_rotation(static_cast<zcls::Node*>(object(n)), &out[0], &out[1], &out[2]);
  }
  const float* entity_position(zgame::Handle e) override {
    world_position(g_sensors.node(e, false), place_);
    return place_;
  }
  // vt+0xdc: the Mech's 0x00418f30, a basic vehicle's 0x004a3630. A turret's
  // slot is empty (never the player's vehicle).
  void place(zgame::Handle v, const float pos[3], float yaw) override {
    if (Mech* m = mech(v)) return m->set_place(pos, yaw);
    if (void* b = vehicle(v)) return program_vehicle_place(b, pos, yaw);
    AnimOwnerHooks::place(v, pos, yaw);
  }
  // The binder's: zEffect's defs and nodes, zClass's parents, the valves.
  zgame::Handle anims_next(zgame::Handle a) override { return handle(zeff::anim_next_with_callbacks(def(a))); }
  void anim_callback_values(zgame::Handle a, std::vector<int32_t>& out) override {
    if (zeff::AnimDef* d = def(a))
      zeff::anim_each_callback(d, [](zeff::AnimDef*, void* v, int32_t x) { static_cast<std::vector<int32_t>*>(v)->push_back(x); }, &out);
  }
  zgame::Handle anim_node(zgame::Handle a) override { return handle(zeff::anim_node(def(a))); }
  zgame::Handle node_entity(zgame::Handle n) override { return handle(mech3::node_entity(object(n))); }  // 0x0040eee0
  zgame::Handle node_parent(zgame::Handle n) override {
    const auto* nd = static_cast<const zcls::Node*>(object(n));
    return nd && nd->parent_count() == 1 && nd->parents ? handle(nd->parents[0]) : 0;
  }
  zgame::Handle valve_of_node(zgame::Handle n) override {  // 0x004d3c40 on the node's name
    const auto* nd = static_cast<const zcls::Node*>(object(n));
    return nd ? handle(ai::valve_get(nd->name())) : 0;
  }
};
ProgramAnimOwner g_anim_owner;

// The entity classes' facts (entity_classes.h) on the program's objects:
// the mechs, the basic vehicles and field bases, the turrets, targets.zrd's
// objects and the command marker. No network mechs in the port.
struct ProgramEntityFacts : EntityFacts {
  float place_[3] = {};
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override { return log_unported(key, args); }
  EntityClass class_of(zgame::Handle o) override {
    if (!o) return EntityClass::none;
    if (mech(o)) return EntityClass::mech;
    if (void* v = vehicle(o)) return program_is_field_base(v) ? EntityClass::field_base : EntityClass::vehicle;
    if (program_turret_by_handle(o)) return EntityClass::turret;
    if (ai::CZAiTarget* t = target(o)) return t->trap ? EntityClass::trap : EntityClass::target;
    if (program_pocket_marker(o)) return EntityClass::marker;
    return EntityClass::none;
  }
  zgame::Handle blip(BlipTable t, int32_t kind) override {
    switch (t) {
      case BlipTable::mech:
        if (!mech_radar_blip(0)) mech_radar_blips_create();  // our addition: made when first needed (0x00413e60's part)
        return handle(mech_radar_blip(kind));
      case BlipTable::vehicle: return handle(program_vehicle_marker(kind));
      case BlipTable::turret: return handle(program_turret_marker(kind));
      case BlipTable::target: return ai::target_marker(kind);
      default: return 0;
    }
  }
  uint32_t team(zgame::Handle o) override {
    const ai::AiTarget* a = g_sensors.fields(o);
    return a ? a->team : 0;
  }
  float height(zgame::Handle o) override {
    const ai::AiTarget* a = g_sensors.fields(o);
    return a ? a->height : 0.0f;
  }
  uint8_t target_e8(zgame::Handle o) override {
    const ai::CZAiTarget* t = target(o);
    return t ? t->desc.flags : 0;
  }
  const char* target_name(zgame::Handle o) override {
    const ai::CZAiTarget* t = target(o);
    return t && t->desc.named ? t->desc.name.c_str() : nullptr;
  }
  const char* pilot_name(zgame::Handle o) override {  // 0x004aee70
    if (Mech* m = mech(o)) return mech_brain_name(m);
    void* v = vehicle(o);
    const auto* b = v ? static_cast<const ai::Brain*>(program_vehicle_brain(program_vehicle_controls(v))) : nullptr;
    return b ? b->params.pilot : nullptr;
  }
  zgame::Handle player_vehicle() override { return g_sensors.player(); }
  const char* player_name() override { return program_pilot_name(); }
  void place(zgame::Handle o, float out[3]) override { world_position(g_sensors.node(o, false), out); }
  void target_centre(zgame::Handle o, float out[3]) override { world_position(g_sensors.node(o, true), out); }
};
ProgramEntityFacts g_entity_facts;

}  // namespace

void wire_sensors(const MechWiring* w, const char* (*message)(void* e, int32_t id)) {
  g_w = w ? *w : MechWiring{};
  g_message = message;
  ai::set_sensor_hooks(w ? &g_sensors : nullptr);
  ai::set_anim_owner_hooks(w ? &g_anim_owner : nullptr);
  set_entity_facts(w ? &g_entity_facts : nullptr);
}

// The HUD's contact objects (app_sensors.h).
void* hud_contact_vehicle(const void* c) { return object(contact_vehicle_of(static_cast<const ai::Contact*>(c)->seen)); }
void* hud_contact_entity(const void* c) { return object(static_cast<const ai::Contact*>(c)->entity); }
float hud_contact_size(const void* c) { return contact_size_of(static_cast<const ai::Contact*>(c)->seen); }
uint32_t hud_object_team(const void* o) { return entity_team(handle(o)); }
bool hud_object_classed(const void* o) { return entity_facts().class_of(handle(o)) != EntityClass::none; }
void* hud_object_blip(const void* o, int32_t kind) { return object(entity_blip(handle(o), kind)); }
bool hud_object_hidden(const void* o) { return entity_hidden(handle(o)); }
void* hud_object_mech(const void* o) { return object(entity_mech(handle(o))); }
const char* hud_object_name(const void* o) { return o ? entity_name(handle(o)) : nullptr; }
void hud_object_place(const void* o, float out[3]) { g_entity_facts.place(handle(o), out); }
void hud_object_centre(const void* o, float out[3]) { entity_centre(handle(o), out); }
void hud_object_centroid(const void* o, float out[3]) { world_position(g_sensors.node(handle(o), true), out); }
void* hud_object_node(const void* o) { return g_sensors.node(handle(o), false); }
// 0x005a8630: the target node's (+0x10) place; vt+0xc 0x005a82d0: zEntity's
// velocity (+0x30), the basic vehicles' kept by them, the rest still.
void hud_object_aim_place(const void* o, float out[3]) {
  const zgame::Handle h = handle(o);
  zgame::Handle n = 0;
  if (void* v = vehicle(h)) return world_position(static_cast<zcls::Node*>(program_vehicle_target_node(v)), out);
  if (tur::Turret* t = program_turret_by_handle(h)) n = t->target_node;
  else if (ai::CZAiTarget* t = target(h)) n = t->target_node;
  world_position(static_cast<zcls::Node*>(object(n)), out);
}
// Mech3.exe 1.2 @ 0x005a8630: the target node's world position, refreshed
// first when stale; the weapons' lock keeps the pointer and follows it.
const float* weapon_lock_point(const void* o) {
  zcls::Node* n = aim_node(handle(o));
  if (!n || !n->data || n->cls() != 5) return nullptr;
  float p[3];
  world_position(n, p);
  return static_cast<zcls::Object3d*>(n->data)->floats(0x84);
}
// vt+0xc: a mech's 0x0041d480 writes its lead velocity (0x0041d400) into
// +0x64c and hands that out, so a lock keeps the velocity at the lock; a
// basic vehicle's 0x005a82d0 is its live +0x30; the rest stand still.
const float* weapon_lock_velocity(void* o) {
  static const float still[3] = {0, 0, 0};
  const zgame::Handle h = handle(o);
  if (Mech* m = mech(h)) {
    if (m->controls) m->lead_velocity(&m->lock_velocity);
    return &m->lock_velocity.x;
  }
  if (void* v = vehicle(h)) return program_vehicle_velocity_at(v);
  return still;
}
void* hud_target_marker(int32_t kind) { return object(ai::target_marker(kind)); }
// vt+0x68: a mech's 0x0041b9d0, a turret's 0x004a75b0, a basic vehicle's
// and a field base's 0x004ad390; a target's and a marker's 0x004a5bc0 (empty).
void hud_object_view_tint(const void* o, bool on) {
  const zgame::Handle h = handle(o);
  if (Mech* m = mech(h)) return mech_view_tint(*m, on);
  if (tur::Turret* t = program_turret_by_handle(h)) return t->tint(on);
  if (void* v = vehicle(h)) program_vehicle_view_tint(v, on);
}
bool hud_object_is_target(const void* o) {
  const EntityClass c = entity_facts().class_of(handle(o));
  return c == EntityClass::target || c == EntityClass::trap;
}
bool hud_object_is_field_base(const void* o) {
  void* v = vehicle(handle(o));
  return v && program_is_field_base(v);
}
void hud_object_velocity(const void* o, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  if (void* v = vehicle(handle(o))) program_vehicle_velocity(v, out);
}

int32_t sensed_contacts(const void* mech, SensedContact* out, int32_t cap) {
  const Mech* m = static_cast<const Mech*>(mech);
  if (!m) return 0;
  int32_t n = 0;
  for (const ai::Contact* c : m->contacts) {
    if (n < cap) {
      out[n].seen = c->seen;
      out[n].distance = std::sqrt(c->dist2);
      out[n].in_sight = c->flags & 1;
    }
    ++n;
  }
  return n;
}

int32_t selected_contact(const void* mech) {
  const Mech* m = static_cast<const Mech*>(mech);
  if (!m || !m->target) return -1;
  for (size_t i = 0; i < m->contacts.size(); ++i)
    if (m->contacts[i] == m->target) return static_cast<int32_t>(i);
  return -1;
}

// ---- --campaign-chain's kills (ours) ----

// Each class's zEntity active flag (+0x08); a mech also not in its dead
// state (10).
bool chain_alive(uint32_t h) {
  if (const Mech* m = mech(h)) return m->active && m->anim_state != 10;
  if (void* v = vehicle(h)) return program_vehicle_active(v) != 0;
  if (const tur::Turret* t = program_turret_by_handle(h)) return t->active != 0;
  if (const ai::CZAiTarget* t = target(h)) return t->active && !t->destroyed;
  return false;
}

std::vector<ChainTarget> chain_hostiles() {
  std::vector<ChainTarget> out;
  const auto* player = static_cast<const Mech*>(mech_mission_player());
  if (!player) return out;
  const uint32_t friends = player->team | player->allies;
  // Team 0's bit 0 is no one's enemy, but its targets.zrd objects are shot
  // too: c1m3's buildings fall only once the legs (team 0) under them have.
  auto add = [&](uint32_t h, const char* kind, bool neutral = false) {
    const ai::AiTarget* a = g_sensors.fields(h);
    if (!a || h == handle(player) || (a->team & friends) || !chain_alive(h)) return;
    if ((a->team & 1u) && !neutral) return;
    for (const ChainTarget& t : out)
      if (t.h == h) return;
    const zcls::Node* node = g_sensors.node(h, false);  // its node's name: the scripts' and objectives'
    out.push_back({h, kind, node ? std::string(node->name()) : std::string("?")});
  };
  for (const uint32_t e : ai::entity_list())
    add(e, mech(e) ? "mech" : vehicle(e) ? "vehicle" : program_turret_by_handle(e) ? "turret" : "entity");
  for (tur::Turret* t : tur::turret_list()) add(handle(t), "turret");
  for (ai::CZAiTarget* t : ai::target_list()) add(t->self, "target", true);
  return out;
}

void* chain_object(uint32_t h) { return object(h); }

std::vector<int32_t> chain_weapon_types(uint32_t h) { return salvage_weapon_types(object(h)); }

void* chain_weapon() {
  for (zwep::WeaponDef& d : zwep::state().defs)
    if (!(d.flags & (zwep::kBeam | zwep::kTargetHeat)) && !d.callback && d.type && !d.type->is_narc && d.damage > 1.0f)
      return &d;
  return nullptr;
}

namespace {
// The highest ground at (x, z) that is not water, every zone's, below `below`
// when given (a cave's floor, not its roof); `fallback` when there is none.
float ground_at(float x, float z, float fallback, float below = 1e30f) {
  zcls::Node* w = world_hooks().world;
  if (!w) return fallback;
  float probe[3] = {x, 2000.0f, z};
  static zcls::GroundHits hits;
  const int32_t mode = zmod::zones().mode;
  zmod::zones().mode = 0;
  zcls::ground_query(w, probe, 1, 2000.0f, &hits);
  zmod::zones().mode = mode;
  float y = fallback;
  bool found = false;
  for (int i = 0; i < hits.count; ++i) {
    if (hits.hit[i].material && hits.hit[i].material->soil == 1) continue;
    if (hits.hit[i].height > below) continue;
    if (!found || hits.hit[i].height > y) y = hits.hit[i].height, found = true;
  }
  return y;
}
}  // namespace

bool chain_player_near(void* node, float dist, bool at_its_level) {
  auto* player = static_cast<Mech*>(mech_mission_player());
  auto* n = static_cast<zcls::Node*>(node);
  if (!player || !n) return false;
  float at[3], from[3];
  world_position(n, at);
  world_position(static_cast<zcls::Node*>(player->node), from);
  float dx = from[0] - at[0], dz = from[2] - at[2];
  const float len = std::sqrt(dx * dx + dz * dz);
  if (len <= dist * 1.5f) return true;  // near enough already
  if (len > 0.001f) dx = dx / len, dz = dz / len;
  else dx = 1.0f, dz = 0.0f;
  const float x = at[0] + dist * dx, z = at[2] + dist * dz;
  const float to[3] = {x, at_its_level ? ground_at(x, z, at[1], at[1] + 10.0f) : ground_at(x, z, at[1]), z};
  move_mech(player, to, std::atan2(dx, dz));  // facing it: forward is -z
  return true;
}

bool chain_handle_at(uint32_t h, float out[3]) {
  zcls::Node* n = g_sensors.node(h, false);
  if (!n) return false;
  world_position(n, out);
  return true;
}

bool chain_player_face(const float at[3]) {
  auto* player = static_cast<Mech*>(mech_mission_player());
  if (!player) return false;
  float from[3];
  world_position(static_cast<zcls::Node*>(player->node), from);
  const float dx = from[0] - at[0], dz = from[2] - at[2];
  if (dx * dx + dz * dz < 1e-6f) return false;
  move_mech(player, from, std::atan2(dx, dz));  // facing it, where it stands: forward is -z
  return true;
}

bool chain_valve_link_at(const char* valve, int32_t value, float out[3]) {
  const ai::Valve* want = ai::valve_find(valve);
  if (!want) return false;
  bool found = false;
  for (auto& [name, file] : ai::net_files()) {
    if (!file || found) continue;
    file->add_ref();  // the nets are built and their files unloaded; read again for a moment
    for (int32_t i = 0; i < file->record_count() && !found; ++i) {
      const char* key = nullptr;
      const ai::NetNodeRec* r = file->node(i, &key);
      if (!r || r->kind != 1 || !r->valve_assign || r->valve_assign->valve != want ||
          r->valve_assign->value != value || r->links.empty()) continue;
      char from_key[16];
      std::snprintf(from_key, sizeof from_key, "node_%02d", r->links[0]);
      if (const ai::NetNodeRec* from = file->node_named(from_key)) std::copy(from->pos, from->pos + 3, out), found = true;
    }
    file->release();
  }
  return found;
}

bool chain_player_at(const float at[3]) {
  auto* player = static_cast<Mech*>(mech_mission_player());
  if (!player) return false;
  const float to[3] = {at[0], ground_at(at[0], at[2], at[1], at[1] + 10.0f), at[2]};  // its floor, not a roof over it
  move_mech(player, to, 0.0f);
  return true;
}

bool chain_entity_at(const char* name, float out[3]) {
  for (const uint32_t e : ai::entity_list()) {
    zcls::Node* n = g_sensors.node(e, false);
    if (n && std::strcmp(n->name(), name) == 0) return world_position(n, out), true;
  }
  return false;
}

bool chain_player_to(const char* name, float dist) {
  for (const uint32_t e : ai::entity_list()) {
    zcls::Node* n = g_sensors.node(e, false);
    if (n && std::strcmp(n->name(), name) == 0) return chain_player_near(n, dist);
  }
  return false;
}

bool chain_player_to_region(const char* name) {
  auto* player = static_cast<Mech*>(mech_mission_player());
  ai::Region* r = ai::region_named(name);
  zcls::Node* w = world_hooks().world;
  if (!player || !r || r->elems.empty() || !w) return false;
  float mid[3] = {0, 0, 0};
  for (size_t i = 0; i < r->elems.size(); ++i)
    for (int k = 0; k < 3; ++k) mid[k] += r->edge(static_cast<int32_t>(i))->point[k];
  for (float& v : mid) v = v / static_cast<float>(r->elems.size());
  const float to[3] = {mid[0], ground_at(mid[0], mid[2], mid[1]), mid[2]};
  move_mech(player, to, 0.0f);
  return true;
}

namespace {
bool shootable(const zcls::Node* n) {
  const auto* t = n ? static_cast<const zwep::NodeHandlers*>(n->damage) : nullptr;
  return t && t->slot[0].fn;
}
// The first node at or under n a shot can damage (a damage handler, its own
// or handed down), depth first.
zcls::Node* shootable_under(zcls::Node* n, int depth = 0) {
  if (!n || depth > 16) return nullptr;
  if (shootable(n)) return n;
  for (int32_t i = 0; i < n->child_count(); ++i)
    if (zcls::Node* c = shootable_under(n->children[i], depth + 1)) return c;
  return nullptr;
}
}  // namespace

bool chain_hit_node(void* node, void* def) {
  auto* d = static_cast<zwep::WeaponDef*>(def);
  auto* player = static_cast<Mech*>(mech_mission_player());
  zcls::Node* n = shootable_under(static_cast<zcls::Node*>(node));
  if (!d || !player || !n) return false;
  float at[3], from[3];
  world_position(n, at);
  world_position(static_cast<zcls::Node*>(player->node), from);
  // From 30 m off along the line to the player: the knockback's direction.
  float dx = from[0] - at[0], dz = from[2] - at[2];
  const float len = std::sqrt(dx * dx + dz * dz);
  if (len > 0.001f) dx = dx / len, dz = dz / len;
  else dx = 1.0f, dz = 0.0f;
  zwep::HitRecord rec;
  rec.normal = {0.0f, 1.0f, 0.0f};
  rec.point = {at[0], at[1], at[2]};
  rec.node = static_cast<zwep::Handle>(n);
  static zwep::Projectile p;
  p = zwep::Projectile{};
  p.owner = player->node;
  p.position = {at[0] + 30.0f * dx, at[1], at[2] + 30.0f * dz};
  p.spawn_at = p.position;
  p.damage_scale = 1.0f;
  zwep::impact(d, &rec, &p, nullptr);
  return true;
}

std::vector<void*> chain_prerequisite_nodes() {
  std::vector<void*> out;
  for (ai::CZAiTarget* t : ai::target_list()) {
    auto* d = static_cast<zeff::AnimDef*>(object(t->desc.destroy_anim));
    if (!t->destroyed || !d || d->raw[0x98] != 1) continue;  // its destroy animation never ran
    for (int32_t k = 0; k < d->count(0x110); ++k) {
      const zeff::Prerequisite& q = d->prereqs[k];
      if (q.raw[4] != 1) continue;
      zeff::AnimDef* p = q.def ? q.def : zeff::anim_find(reinterpret_cast<const char*>(q.raw + 8));
      // Its root holds its own damage handler (0x0050ae60's set_damage_handler):
      // where a shot of the mesh lands, under nodes handed the target's.
      if (p && p->raw[0x98] == 1 && (p->root || p->node)) out.push_back(p->root ? p->root : p->node);
    }
  }
  return out;
}

bool chain_hit(uint32_t h, void* def) {
  zcls::Node* n = aim_node(h);
  if (!shootable(n)) n = g_sensors.node(h, false);
  if (!chain_hit_node(n, def)) return false;
  return !chain_alive(h);
}

}  // namespace mech3
