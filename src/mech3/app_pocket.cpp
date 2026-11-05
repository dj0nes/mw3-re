// The pocket commander (the lance command mode, docs/ai.md 17.6a..e) in the
// program: its panel's names, the command target marker made at the
// mission's start, the cursor's picks through the HUD's reticle, the node
// marks. The mission map's (0x00491c80) and a network game's pieces, the
// repair bays (0x0045a000) and the view's pocket layer (0x004b5290) are not
// ported and stay unwired (the panel answers as without them).
#include <cstring>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zinput/zinput.h"
#include "gamez/ztwod/twod_host.h"
#include "mech3/app_mech.h"
#include "mech3/app_view.h"
#include "mech3/hud.h"
#include "mech3/mech.h"
#include "mech3/mech_command.h"
#include "mech3/mech_hud.h"
#include "mech3/mech_pocket.h"
#include "mech3/pocket_panel.h"
#include "mech3/sensors.h"
#include "mech3/mission_world.h"
#include "gamez/zclass/cls_di.h"
#include "mech3/mission_map.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_params.h"

namespace mech3 {

const char* program_pilot_name();  // app_menus.cpp

namespace {
MechWiring g_w;
PocketMarker* g_marker = nullptr;

zgame::Handle handle(const void* p) { return p && g_w.handle_of ? g_w.handle_of(g_w.engine, p) : 0; }
void* object(zgame::Handle h) { return h && g_w.object_of ? g_w.object_of(g_w.engine, h) : nullptr; }
Mech* mech(zgame::Handle h) {
  void* o = object(h);
  return entity_is_mech(o) ? static_cast<Mech*>(o) : nullptr;
}
zcls::Node* zn(void* p) { return static_cast<zcls::Node*>(p); }
zcls::Node* entity_node_of(zgame::Handle e) {
  if (Mech* m = mech(e)) return zn(m->node);
  if (PocketMarker* k = program_pocket_marker(e)) return zn(k->node);
  return nullptr;
}
// An object3d's world place (+0x84 of its data), its matrix not refreshed
// (0x005a85e0's pointer).
const float* node_place(zcls::Node* n) {
  return n && n->data && n->cls() == 5 ? static_cast<zcls::Object3d*>(n->data)->floats(0x84) : nullptr;
}
Hud* view_hud(Mech& m) { return &m == program_view_target() && m.pilot ? pilot_hud(*m.pilot) : nullptr; }
}  // namespace

PocketMarker* program_pocket_marker(zgame::Handle h) {
  return h && g_marker && h == pocket_globals().marker && object(h) == g_marker ? g_marker : nullptr;
}

// The mission's end on the marker's entry (vt+0x00): zEntity3d's base end
// 0x004d58a0 frees its caster (0x004814e0 made one within 100 m).
bool program_pocket_marker_end(zgame::Handle h) {
  if (!h || !g_marker || object(h) != g_marker) return false;  // the panel's globals may be gone by then
  g_marker->release_shadow();
  return true;
}

void wire_pocket(const MechWiring* w) {
  PanelCalls& pc = panel_calls();
  FrameCalls& fc = frame_calls();
  DrawCalls& dc = draw_calls();
  if (!w) {
    g_w = MechWiring{};
    pc.unit_name = nullptr, pc.node_entity = nullptr, pc.entity_team = nullptr;
    pc.make_marker = nullptr, pc.marker_on = nullptr;
    fc = FrameCalls{};
    dc.node_marked = nullptr;
    return;
  }
  g_w = *w;
  // The lancemates' names (vt+0xa4, 0x0041df30): the brain's pilot (0x004aee70).
  pc.unit_name = [](Mech& m) -> const char* { return mech_brain_name(&m); };
  pc.node_entity = [](uint32_t n) -> zgame::Handle { return handle(node_entity(object(n))); };  // 0x0040eee0
  pc.entity_team = [](zgame::Handle e) -> uint32_t {
    const ai::AiTarget* t = ai::brain_hooks().targetable(e);
    return t ? t->team : 0;  // vt+0x28's +0xc0
  };
  // 0x00468100("cmd_target.flt", 0): the world's node of that name, else a
  // new object3d so named (out of the lists' flag 8 and the queries); under
  // the world when it has no parent ([0x00608c84] vt+0x00); an AiTarget on it
  // (0x004d7f80: zEntity3d's bind 0x0040eaa0, the node's +0x28 bit 16 and
  // owner, and zEntity's list), named message 0x89d (0x005a84c0), on the
  // player's side (0x004d5af0(0), [0x0060b56c] cleared).
  pc.make_marker = []() -> zgame::Handle {
    zcls::Node* n = zcls::list_find_by_name(6, "cmd_target.flt");
    if (!n) {
      n = zcls::object3d_create();
      if (!n) return 0;
      zcls::node_set_name(n, "cmd_target.flt");
      zcls::node_set_flag8(n, 0);
      zcls::node_set_intersect(n, 0);
    }
    if (n->parent_count() == 0)
      if (zcls::Node* wn = world_hooks().world) zcls::node_add_child(wn, n);
    // The previous mission's marker (the exe leaves it to the world).
    if (g_marker) {
      auto& list = ai::entity_list();
      for (auto it = list.begin(); it != list.end();) it = object(*it) == g_marker ? list.erase(it) : std::next(it);
      // Its handle too (ours): else the next object at its address takes the
      // old handle (the next mission's mechs did, by allocation luck).
      if (g_w.forget_object) g_w.forget_object(g_w.engine, g_marker);
      delete g_marker;
    }
    g_marker = new PocketMarker;
    g_marker->node = n;
    n->owner = g_marker;
    n->put<uint32_t>(0x28, n->get<uint32_t>(0x28) | 0x10000);
    g_marker->name = ztwod::host().message_text(0x89d);  // 0x00576040
    g_marker->set_team(static_cast<uint32_t>(ai::team_side(0)));
    const zgame::Handle h = handle(g_marker);
    ai::entity_list().push_back(h);
    return h;
  };
  pc.marker_on = [](zgame::Handle h, bool on) {  // 0x004eb170 on its node, +0x08
    if (PocketMarker* k = program_pocket_marker(h)) {
      if (k->node) zcls::node_set_active(zn(k->node), on ? 1 : 0);
      k->active = on ? 1 : 0;
    }
  };

  // The frame's (0x00469d00): the viewed HUD (0x00430aa0) and its radar's
  // map flag (+0x70, the radar's +0x5c); the reticle's aim record (0x00444330,
  // +0x928: its node +0x24) and spot (+0x8fc); the trigger (command 0x3e).
  fc.view_hud = [](Mech& m) -> zgame::Handle { return handle(view_hud(m)); };
  fc.hud_map_up = [](zgame::Handle h) {
    const Hud* hud = static_cast<const Hud*>(object(h));
    return hud && hud->radar.active == 1;
  };
  // The map pick (0x00469d00 with the mission map up): the map (0x00491c80
  // on the missions object, its +0xbc), its size (the rect's right and
  // bottom), the cursor on it (0x00444670: the reticle's vt+0x68 / +0x6c)
  // and the spot and node under it (0x004587b0 on the map). Until
  // 2026-10-30 none was set: the panel never saw the map up, and a click on
  // it ordered at the reticle's aim.
  fc.map = []() -> zgame::Handle { return handle(mission_map()); };
  fc.map_size = [](zgame::Handle map, int32_t* w, int32_t* h) {
    const auto* m = static_cast<const MissionMap*>(object(map));
    *w = m && m->rect ? m->rect->right : 0;  // +0xd8's +0x08, +0x0c
    *h = m && m->rect ? m->rect->bottom : 0;
  };
  fc.map_mouse = [](zgame::Handle h, int32_t* x, int32_t* y) {
    Hud* hud = static_cast<Hud*>(object(h));
    *x = hud ? hud->reticle.get_x() : 0;
    *y = hud ? hud->reticle.get_y() : 0;
  };
  fc.map_pick = [](float x, float y, float place[3], uint32_t* node) {
    const auto* m = static_cast<const MissionMap*>(mission_map());
    zcls::GroundHit hit{};
    auto ground = [](float gx, float gy, float gz, zcls::GroundHits* out) -> int32_t {  // 0x004edab0 on [0x00608c84]+4
      zcls::Node* w = world_hooks().world;
      return w ? zcls::ground_point(w, gx, gy, gz, out) : 1;
    };
    const bool hit_it = m && m->pick(x, y, place, &hit, world_hooks().world ? +ground : nullptr);
    if (node) *node = hit_it ? handle(hit.node) : 0;  // the record's +0x24
    return hit_it;
  };
  fc.reticle_pick = [](zgame::Handle h) -> zgame::Handle {
    Hud* hud = static_cast<Hud*>(object(h));
    return hud ? handle(&hud->reticle.hit) : 0;
  };
  fc.record_node = [](zgame::Handle r) -> uint32_t {
    const auto* rec = static_cast<const zwep::HitRecord*>(object(r));
    return rec ? handle(rec->node) : 0;
  };
  fc.reticle_place = [](zgame::Handle h, float place[3]) {
    if (const Hud* hud = static_cast<const Hud*>(object(h))) std::memcpy(place, hud->reticle.aim_point, 12);
  };
  fc.trigger = []() -> uint32_t {
    ControlsCalls& k = controls_calls();
    return k.command ? k.command(0x3e) : zin::command(0x3e);  // 0x00467480
  };
  fc.contact_entity = [](void* c) -> zgame::Handle {  // 0x004e6fe0
    return ai::brain_hooks().contact_entity(static_cast<ai::Contact*>(c));
  };
  fc.entity_node = [](zgame::Handle e) -> uint32_t { return handle(entity_node_of(e)); };  // +0x04
  fc.entity_place = [](zgame::Handle e) -> const float* { return node_place(entity_node_of(e)); };  // 0x005a85e0
  fc.entity_name = [](zgame::Handle e) -> const char* {  // vt+0xa4
    if (Mech* m = mech(e)) return m == world().mission_player ? program_pilot_name() : mech_brain_name(m);
    if (PocketMarker* k = program_pocket_marker(e)) return k->name;
    return nullptr;
  };
  // 0x004a7b40: the entity already the target, nothing; else the contact on
  // it the target (vt+0xc0, 0x0041d890: AiTarget's, then TARGET on the
  // view's HUD), none when there is none.
  fc.pick_target = [](Mech& m, zgame::Handle e) {
    if (m.target && m.target->seen == e) return;
    ai::Contact* found = nullptr;
    for (ai::Contact* c : m.contacts)
      if (c->seen == e) {
        found = c;
        break;
      }
    m.set_target(found, false);
    if (Hud* hud = view_hud(m))
      if (command_calls().hud) command_calls().hud(hud, 0x49);
  };
  fc.marker_move = [](zgame::Handle h, const float* p) {  // vt+0x18: the node moved there
    PocketMarker* k = program_pocket_marker(h);
    if (k && k->node && p) zcls::object3d_set_translation(zn(k->node), p[0], p[1], p[2]);
  };
  // The node mark (0x0040ee50): +0x28 bit 21.
  dc.node_marked = [](uint32_t n) {
    const zcls::Node* z = zn(object(n));
    return z && ((z->get<uint32_t>(0x28) >> 21) & 1) != 0;
  };
}

}  // namespace mech3
