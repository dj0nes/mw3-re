// The repair dock's states and the field bases' drives (docs/vehicles.md,
// The repair dock; mech_fieldbase.h). Same original file as
// mech_fieldbase.cpp (0x004598a0..0x0045b9c0).
#include <algorithm>
#include <cmath>

#include <cstdio>
#include <cstring>

#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zfix.h"
#include "mech3/mech_fieldbase.h"
#include "mech3/zai_brain.h"
#include "gamez/zutil/zfix.h"
#include "mech3/zai_valve.h"
#include "mech3/zainet.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
FieldBaseHooks& hk() { return field_base_hooks(); }

// The dock point's radius, squared ([0x005e4a18], 12 m), and a base's
// stand-off from it ([0x005e4a10]; the near ring's radius, [0x005e4a28]).
constexpr float kNear2 = 144.0f;
constexpr float kStandOff = 15.4f;
// The multiplayer stations' reach, squared: 20 m to be asked ([0x005e4a1c]),
// 6.5 m to repair ([0x005e4a20]), no enemy within 100 m ([0x005e4a24]).
constexpr float kStationAsk2 = 400.0f;
constexpr float kStationRepair2 = 42.5f;
constexpr float kStationEnemy2 = 10000.0f;
Handle g_repair_anim = nullptr;  // [0x00605c48]

// A drive made for each base and queued at the front of its steps, begun at
// its next frame, when it is active. FIX (bug): the exe follows a missing
// base (a mission with fewer than three); none is made for it.
void queue_drives(FieldBases& r, FieldBaseDrive* (&d)[3], bool open) {
  for (int i = 0; i < 3; ++i) {
    FieldBase* b = r.bases[i];
    d[i] = b ? hk().drive_make(b, open) : nullptr;
  }
  for (FieldBaseDrive* x : d) {
    if (!x || !x->base->active) continue;
    x->base->actions.push_front(x);
    x->base->begin_next = true;
  }
}
// 0x004598c0 and the delete. FIX (bug): a drive still on its base's steps
// (its state ended before the base dropped it) is left to the base, which
// frees it as it drops it: the exe reads the freed drive until then.
void free_drives(FieldBaseDrive* (&d)[3]) {
  for (FieldBaseDrive*& x : d) {
    if (!x) continue;
    std::list<DockStep*>* l = x->base ? &x->base->actions : nullptr;
    if (l && std::find(l->begin(), l->end(), x) != l->end()) x->orphan = true;
    else hk().drive_free(x);
    x = nullptr;
  }
}
bool drives_done(FieldBaseDrive* const (&d)[3]) {
  for (FieldBaseDrive* x : d)
    if (x && !x->done()) return false;
  return true;
}
}  // namespace

float FieldBaseHooks::game_time() { return zgame::from_bits(unported("[0x0060991c]", {})); }
uint32_t FieldBaseHooks::frame() { return unported("[0x0072f338]", {}); }
int32_t FieldBaseHooks::valve_value(ai::Valve* v) { return v->get(); }
ai::Valve* FieldBaseHooks::valve_find(const char* name) { return ai::valve_find(name); }
Handle FieldBaseHooks::base_brain(FieldBase& b) {
  auto* c = dynamic_cast<ai::AiVehicleControls*>(b.controls);
  return c ? c->brain.get() : nullptr;
}
Handle FieldBaseHooks::brain_order_net(Handle brain) {
  ai::Brain* b = static_cast<ai::Brain*>(brain);
  ai::Order& o = b->current();
  return o.net ? o.net->net() : nullptr;
}
Handle FieldBaseHooks::brain_behaviour(Handle brain) {
  ai::Order& o = static_cast<ai::Brain*>(brain)->current();
  return o.state ? o.state->behaviour() : nullptr;
}
// 0x0058b0a0: the first element whose name (vt+0x24) is `name`.
Handle FieldBaseHooks::net_element(Handle net, const char* name) {
  auto* n = static_cast<ai::AiNet*>(net);
  for (znet::Element* e : n->elems) {
    const auto* st = dynamic_cast<const ai::NodeStep*>(e);
    if (st && st->has_name && std::strcmp(name, st->name.c_str()) == 0) return e;
  }
  return nullptr;
}
Handle FieldBaseHooks::element_behaviour(Handle e) {
  return static_cast<ai::AiElem*>(static_cast<znet::Element*>(e))->as_behaviour();
}
int32_t FieldBaseHooks::link_count(Handle beh) { return static_cast<ai::Behaviour*>(beh)->link_count(); }
Handle FieldBaseHooks::link(Handle beh, int32_t i) {
  return static_cast<znet::Element*>(static_cast<ai::Behaviour*>(beh)->link(i));
}
void FieldBaseHooks::brain_path_reset(Handle brain) { static_cast<ai::Brain*>(brain)->path.reset(); }
void FieldBaseHooks::brain_go_to(Handle brain, Handle net, Handle target) {
  static_cast<ai::Brain*>(brain)->go_to(static_cast<ai::AiNet*>(net), static_cast<ai::Behaviour*>(target));
}
Handle& field_bases_repair_anim() { return g_repair_anim; }

// ---------------------------------------------------------------------------
// The base step (0x005c2600)

Handle DockStep::message(Handle m) {
  abort();  // vt+0x14
  return m;
}
void DockStep::abort() {
  hk().marker_clear();
  finished = true;
}

// ---------------------------------------------------------------------------
// The drive (0x005c2540)

// Mech3.exe 1.2 @ 0x004598d0: out, the travel is the stand-off less the
// distance from the dock point; in, the distance to its place in.
void FieldBaseDrive::begin() {
  const float* p = hk().entity_position(base);  // 0x005a85e0
  const zmth::Vec3 at{p[0], p[1], p[2]};
  zmth::Vec3 dir{};
  if (open) {
    const zmth::Vec3 dock{base->dock_at[0], base->dock_at[1], base->dock_at[2]};
    travel = -(zmth::direction(dock, at, dir) - kStandOff);  // 0x0051e3d0
  } else {
    const float* q = base->dock_far;
    travel = zmth::direction(at, zmth::Vec3{q[0], q[1], q[2]}, dir);
  }
  base->arms->aim(open);  // 0x004589a0
  reached = false;
  finished = false;
  way = open ? -1.0f : 1.0f;  // [0x005c23b4], [0x005c23a4]
}

// Mech3.exe 1.2 @ 0x00459a80
bool FieldBaseDrive::there(float d) const { return open ? -0.2f < d : d < 0.2f; }

// Mech3.exe 1.2 @ 0x00459990: a quarter throttle its way until the stopping
// distance would carry it there; done with its arms there too.
bool FieldBaseDrive::step(float) {
  const float t = base->arms->now;
  const bool arms = open ? (t == 1.0f || std::isnan(t)) : (t == 0.0f || std::isnan(t));  // test ah, 0x40
  VehicleControls& c = *base->controls;  // vt+0x140
  if (!reached) {
    const float s = hk().stopping_now(*base);  // vt+0x124
    if (there(travel - s * way)) c.throttle = 0.0f;  // IControls vt+0x20
    else c.throttle = static_cast<float>(way * 0.25);
  } else {
    c.throttle = 0.0f;
  }
  return done() || (arms && reached);
}

// Mech3.exe 1.2 @ 0x00459ac0: the controls' +0x78 (the local z speed) adds up.
void FieldBaseDrive::after(float dt) {
  travel = base->controls->x70.z * dt + travel;
  reached = there(travel);
}

// Mech3.exe 1.2 @ 0x00459af0: the base's front step dropped (this one, the
// deploy's abort assumes), the next begun.
void FieldBaseDrive::abort() {
  end();
  if (base->actions.empty()) return;
  field_base_drop_front(*base);
  base->begin_next = true;
}

// Mech3.exe 1.2 @ 0x0045b9c0
bool FieldBaseDrive::done() { return finished || !base || !base->active; }

// ---------------------------------------------------------------------------
// The registry's helpers

// Mech3.exe 1.2 @ 0x0045a720
bool FieldBases::requester_ok() const { return requester && hk().powered_down(requester); }

// Mech3.exe 1.2 @ 0x0045b740
bool FieldBases::set_requester(Handle m) {
  if (busy || !field_bases_drive()) return false;
  requester = m;
  return true;
}

// Mech3.exe 1.2 @ 0x0045a500
void FieldBases::message(Handle m) { requester = states.empty() ? m : states.front()->message(m); }

// Mech3.exe 1.2 @ 0x0045a540
void FieldBases::abort_front() {
  if (!states.empty()) states.front()->abort();
}

// Mech3.exe 1.2 @ 0x0045b290 / 0x0045b2c0: the three slots, whatever the count.
void FieldBases::effects_on() {
  for (FieldBase* b : bases)
    if (b && b->active) field_base_effects_start(*b);
}
void FieldBases::effects_off() {
  for (FieldBase* b : bases)
    if (b) field_base_effects_stop(*b);
}

// ---------------------------------------------------------------------------
// The states

// Mech3.exe 1.2 @ 0x0045b7d0
void DockState::say_soon() {
  if (sound && field_bases_quiet_until() <= hk().game_time()) hk().say(sound);  // test ah, 0x41
}

// The aborts' unwind (0x0045ade0, 0x0045b1b0, 0x0045b3b0): the front state
// and those after it dropped until `to` is the front; the dropped front is
// ended and `to`, when reached, begun.
void DockState::cancel(DockStep* to) {
  std::list<DockStep*>& l = reg->states;
  if (l.empty() || l.front() == to) return;
  DockStep* const was = l.front();
  DockStep* at = nullptr;
  do {
    at = l.front();
    if (at == to) break;
    l.pop_front();
  } while (!l.empty());
  if (was) was->end();
  if (at == to && at) at->begin();
}

// Mech3.exe 1.2 @ 0x0045a740 / 0x0045a770
void DockIdle::begin() {
  finished = false;
  asked = false;
  reg->idle_on = true;
}
void DockIdle::end() {
  reg->idle_on = false;
  asked = false;
}
// Mech3.exe 1.2 @ 0x0045a780: the mech's request. With the mcbwaynet's dock
// point: the bases may drive, "mcbdeploy" 1 and the point's valve 1, the
// bases' valves 0; each base's brain, on its current net, sent to link i of
// that net's node named after the point (its place); the rings around the
// point; the other six states queued; this one asked.
Handle DockIdle::message(Handle m) {
  FieldBaseHooks& h = hk();
  FieldBases& r = *reg;
  r.requester = m;
  r.dock_point = h.dock_point();
  if (!r.dock_point) return m;
  const char* name = h.dock_point_name(r.dock_point);  // vt+0x24
  if (!name) return m;
  field_bases_drive() = true;
  h.valve_set(r.deploy_valve, 1);
  if (r.point_valve) h.valve_set(r.point_valve, 0);
  r.point_valve = h.valve_find(name);  // 0x004d3d20
  if (r.point_valve) h.valve_set(r.point_valve, 1);
  for (ai::Valve* v : r.valves)
    if (v) h.valve_set(v, 0);
  for (int32_t i = 0; i < 3; ++i) {
    FieldBase* b = r.bases[i];
    if (!b) continue;
    const Handle brain = h.base_brain(*b);
    if (!brain) continue;
    const Handle net = h.brain_order_net(brain);
    const Handle e = h.net_element(net, name);
    const Handle beh = e ? h.element_behaviour(e) : nullptr;
    if (!net || !beh) continue;
    Handle to = nullptr;
    if (i < h.link_count(beh)) {
      const Handle l = h.link(beh, i);
      to = l ? h.element_behaviour(l) : nullptr;
    }
    if (!to) continue;
    h.brain_path_reset(brain);   // +0x238 0x004040d0
    h.brain_go_to(brain, net, to);  // 0x004c84b0
    // FIX (bug): a base already in its slot's behaviour (called again to the
    // point it docked at) has no way to drive and no node to enter, so its
    // valve, set on arriving, never is and the places step waits for good
    // (playtest W8, W16): it counts as arrived.
    if (zfix::options().bugs && r.valves[i] && h.brain_behaviour(brain) == to) h.valve_set(r.valves[i], 1);
  }
  r.deploy_points(h.dock_point_position(r.dock_point));
  for (DockStep* st : {static_cast<DockStep*>(&r.places), static_cast<DockStep*>(&r.contact),
                       static_cast<DockStep*>(&r.deploy), static_cast<DockStep*>(&r.repair),
                       static_cast<DockStep*>(&r.close), static_cast<DockStep*>(&r.reset)})
    r.states.push_back(st);  // 0x0058f2a0 / the list's inline insert, with 0x004d5110
  asked = true;
  return m;
}

// Mech3.exe 1.2 @ 0x0045a560: each base's near point (15.4 off; the
// first ahead in +z, the others 120 degrees round) and far one (45.4).
void FieldBases::deploy_points(const float* at) {
  if (!at) at = hk().entity_position(requester);  // 0x005a85e0
  static const float kFar = kStandOff - -30.0f;  // [0x00605c30], once ([0x00605c38] bit 0)
  for (int ring = 0; ring < 2; ++ring) {
    const float rad = ring ? kFar : kStandOff;
    float(*p)[3] = points + 3 * ring;
    p[0][0] = at[0], p[0][1] = at[1], p[0][2] = rad + at[2];
    p[1][0] = at[0] - rad * -0.866f, p[1][1] = at[1], p[1][2] = at[2] - rad * 0.5f;
    p[2][0] = at[0] - rad * 0.866f, p[2][1] = at[1], p[2][2] = at[2] - rad * 0.5f;
  }
  for (int i = 0; i < 3; ++i) {
    FieldBase* b = bases[i];  // the three slots, unchecked in the exe
    if (!b) continue;
    b->dock_at[0] = at[0], b->dock_at[1] = at[1], b->dock_at[2] = at[2];
    b->dock_near = points[i];
    b->dock_far = points[3 + i];
  }
}

// Mech3.exe 1.2 @ 0x0045a270. Off multiplayer the dock's states run (as a
// field base runs its steps; a step finishing after its own abort marks the
// dropped state, not the new front). In multiplayer, a powered-down player
// within 20 m of a station is repaired there within 6.5 m, unless an enemy
// is within 100 m; otherwise the HUD says why.
void FieldBases::frame(float dt) {
  FieldBaseHooks& h = hk();
  if (h.game_type() != 3) {
    if (states.empty()) return;
    DockStep* f = states.front();
    if (begin_next) {
      f->begin();
      begin_next = false;
      return;
    }
    if (!f->done()) {
      if (f->step(dt)) f->finished = true;
      return;
    }
    f->end();
    states.pop_front();
    if (!states.empty()) states.front()->begin();
    return;
  }
  if (!h.mp_repairs()) return;
  const Handle me = h.player_vehicle();
  if (g_repair_anim || !me || !h.entity_active(me) || !h.entity_shut_down(me)) return;
  const Handle st = h.stations();
  const Handle station = st ? h.station_nearest(st, h.entity_position(me)) : nullptr;
  float d2 = kStationAsk2 - -1.0f;  // [0x005c23b4]
  if (station) {
    const float* a = h.station_position(station);  // vt+0x18
    const float* p = h.entity_position(me);
    const float dx = a[0] - p[0], dy = a[1] - p[1], dz = a[2] - p[2];
    d2 = (dx * dx + dz * dz) + dy * dy;
  }
  if (kStationAsk2 <= d2) return;  // test ah, 1: a NaN goes on
  const Handle c = h.nearest_enemy(me);
  const bool clear = !c || !h.contact_vehicle(c) || h.contact_distance(c) > kStationEnemy2;
  if (station && clear && !(kStationRepair2 <= d2)) {
    field_bases_station_repair(me, station);
    return;
  }
  if (clear) {
    if (kStationRepair2 <= d2) h.hud_message("too far from MFB depolyment point");
    return;
  }
  char text[0x100];
  std::snprintf(text, sizeof text, "%s is too close for MFB deployment", h.entity_name(h.contact_vehicle(c)));
  h.hud_message(text);
}

// Mech3.exe 1.2 @ 0x0045a180: a local mech shut down (+0xdd), the view on
// camera 4, at rest at the station, "mfb_station_01" run on the station's
// node with the end callback, the other players told; a remote one only
// sees the animation.
bool field_bases_station_repair(Handle mech, Handle station) {
  FieldBaseHooks& h = hk();
  VehBasicHooks& vh = veh_basic_hooks();
  if (!station || !mech) return false;
  const Handle def = vh.anim_find("mfb_station_01");  // 0x0050b450
  const Handle node = vh.node_named(h.station_name(station));  // 0x004f7fe0 (6, +0x2c)
  if (!def || !node) return false;
  if (!h.entity_remote(mech)) {  // vt+0x5c
    h.entity_set_shutdown(mech, true);
    if (mech == h.view_vehicle()) h.view_set_camera(4);
    h.mech_rest_at(mech, h.station_position(station));
    g_repair_anim = h.effects_start(def, node);  // 0x00508c10
    h.anim_on_end(g_repair_anim, mech);
    h.station_announce(h.stations(), station);
    return true;
  }
  h.effects_start(def, node);
  return true;
}

// Mech3.exe 1.2 @ 0x0045a140
void field_bases_station_message(Handle mech, uint32_t id) {
  FieldBaseHooks& h = hk();
  const Handle st = h.stations();
  if (st) h.station_nearest(st, h.entity_position(mech));  // its answer unused
  field_bases_station_repair(mech, h.station_by_id(st, id));
}

// Mech3.exe 1.2 @ 0x0045a0f0
void field_bases_station_done(Handle anim, Handle mech, int32_t stopped) {
  FieldBaseHooks& h = hk();
  if (anim != g_repair_anim || stopped) return;
  g_repair_anim = nullptr;
  h.mech_repair(mech, 0.0f);  // 0x004154e0(0)
  h.entity_set_shutdown(mech, false);
  h.mech_power_up(mech);  // 0x0041cc30(0)
  h.view_set_camera(h.view_saved_camera());
}

// Mech3.exe 1.2 @ 0x0045acb0
void DockContact::begin() {
  sound = reg->sounds[7];  // +0xa8 mcb_waiting
  field_bases_contact() = true;
  finished = false;
}
// Mech3.exe 1.2 @ 0x0045ace0: once its line is due, said (and then not
// again); done with the mech near the dock point and powered down.
bool DockContact::step(float) {
  FieldBaseHooks& h = hk();
  if (field_bases_quiet_until() <= h.game_time()) {
    if (sound) h.say(sound);
    field_bases_quiet_until() = 3.4028235e38f;
  }
  const float* at = h.dock_point_position(reg->dock_point);
  const float* p = h.entity_position(reg->requester);
  const float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
  const float d2 = dz * dz + dy * dy + dx * dx;
  return d2 < kNear2 && reg->requester_ok();
}
void DockContact::end() { field_bases_quiet_until() = 0.0f; }  // 0x0045acd0
// Mech3.exe 1.2 @ 0x0045ade0: "cancelled", to the reset.
void DockContact::abort() {
  field_bases_quiet_until() = 0.0f;
  sound = reg->sounds[5];  // +0xa0 mcb_pos_cancel
  say_soon();
  hk().marker_clear();
  cancel(&reg->reset);
}

// Mech3.exe 1.2 @ 0x0045ab50: "wait" after five frames.
void DockPlaces::begin() {
  until = hk().frame() + 5;
  sound = reg->sounds[6];  // +0xa4 mcb_contact
  if (!zfix::options().bugs) return;  // ours: the step's stuck check's start
  for (int i = 0; i < 3; ++i) {
    moved_at[i] = hk().game_time();
    const FieldBase* b = reg->bases[i];
    const float* p = b ? hk().entity_position(static_cast<Vehicle*>(const_cast<FieldBase*>(b))) : nullptr;
    for (int k = 0; k < 3; ++k) mark[i][k] = p ? p[k] : 0.0f;
  }
}
// FIX (bug): how long a base may stand still short of its slot (seconds).
constexpr float kStuck = 30.0f;
// Mech3.exe 1.2 @ 0x0045ac20
bool DockPlaces::step(float) {
  FieldBaseHooks& h = hk();
  if (until < h.frame()) {
    say_soon();
    field_bases_quiet_until() = h.game_time() + 5.0f;
    until = 0x7fffffff;
  }
  // FIX (bug): the exe waits for every base's valve with no give-up, so a base
  // that cannot drive on (no way, blocked) jams the dock for the mission
  // (playtest W8, W16). One that has not moved 2 m in kStuck seconds short of
  // its place counts as in place; the deploy then drives it from where it is.
  if (zfix::options().bugs)
    for (int i = 0; i < 3; ++i) {
      FieldBase* b = reg->bases[i];
      ai::Valve* v = reg->valves[i];
      if (!b || !b->active || !v || h.valve_value(v) != 0) continue;
      const float* p = h.entity_position(static_cast<Vehicle*>(b));
      if (!p) continue;
      const float dx = p[0] - mark[i][0], dz = p[2] - mark[i][2];
      if (dx * dx + dz * dz > 4.0f) {
        for (int k = 0; k < 3; ++k) mark[i][k] = p[k];
        moved_at[i] = h.game_time();
      } else if (h.game_time() - moved_at[i] > kStuck) {
        h.valve_set(v, 1);
      }
    }
  return done();  // vt+0x18
}
// Mech3.exe 1.2 @ 0x0045ac70
void DockPlaces::end() {
  for (ai::Valve* v : reg->valves)
    if (v) hk().valve_set(v, 0);
  finished = false;
}
void DockPlaces::abort() {  // 0x0045ade0, as the contact's
  field_bases_quiet_until() = 0.0f;
  sound = reg->sounds[5];
  say_soon();
  hk().marker_clear();
  cancel(&reg->reset);
}
// Mech3.exe 1.2 @ 0x0045ab70: each base out of play, without a valve, or its
// valve set (by its net: in place). FIX (bug): a missing base counts as in
// place (the exe follows it).
bool DockPlaces::done() {
  if (finished) return true;
  for (int i = 0; i < 3; ++i) {
    const FieldBase* b = reg->bases[i];
    ai::Valve* v = reg->valves[i];
    if (b && b->active && v && hk().valve_value(v) == 0) return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0045af00: busy; the three drives out; the player's view
// onto the dock's camera (mode 4), its old one kept; the mech at rest at the
// dock point.
void DockDeploy::begin() {
  FieldBaseHooks& h = hk();
  reg->busy = true;
  sound = reg->sounds[8];  // +0xac mcb_deploy
  finished = false;
  queue_drives(*reg, drives, true);
  if (reg->requester == h.view_vehicle()) {
    reg->camera = h.view_camera();
    h.view_set_camera(4);
  }
  h.mech_rest_at(reg->requester, h.dock_point_position(reg->dock_point));
}
// Mech3.exe 1.2 @ 0x0045b0e0: the mech powering up aborts.
bool DockDeploy::step(float) {
  if (drives_done(drives)) {
    say_soon();
    return true;
  }
  if (!reg->requester_ok()) abort();  // vt+0x14
  return false;
}
// Mech3.exe 1.2 @ 0x0045b150
void DockDeploy::end() {
  reg->busy = false;
  free_drives(drives);
}
// Mech3.exe 1.2 @ 0x0045b770
Handle DockDeploy::message(Handle m) {
  if (reg->requester != m) hk().say(reg->sounds[3]);  // +0x98 mcb_occupied
  return reg->requester;
}
// Mech3.exe 1.2 @ 0x0045b1b0: "aborted", the drives dropped, to the close.
void DockDeploy::abort() {
  field_bases_quiet_until() = 0.0f;
  sound = reg->sounds[9];  // +0xb0 mcb_abort
  say_soon();
  for (FieldBaseDrive* d : drives)
    if (d) d->abort();  // vt+0x14
  hk().marker_clear();
  cancel(&reg->close);
}

// Mech3.exe 1.2 @ 0x0045b2f0
void DockRepair::begin() {
  reg->busy = true;
  sound = reg->sounds[10];  // +0xb4 mcb_complete
  finished = false;
  timer = 12.0f;
  reg->effects_on();
}
// Mech3.exe 1.2 @ 0x0045b320: the mech repaired when the time is up (with
// this frame's time); powering up first aborts.
bool DockRepair::step(float dt) {
  timer = timer - dt;
  if (timer <= 0.0f) {
    if (reg->requester) hk().mech_repair(reg->requester, dt);
    hk().marker_clear();
    reg->park = zfix::options().bugs;  // FIX (bug), ours: the bases stay (DockParked)
    return true;
  }
  if (!reg->requester_ok()) {
    abort();
    return true;
  }
  return false;
}
// Mech3.exe 1.2 @ 0x0045b380: "complete".
void DockRepair::end() {
  reg->busy = false;
  field_bases_quiet_until() = 0.0f;
  say_soon();
  reg->effects_off();
}
Handle DockRepair::message(Handle m) {  // 0x0045b770
  if (reg->requester != m) hk().say(reg->sounds[3]);
  return reg->requester;
}
// Mech3.exe 1.2 @ 0x0045b3b0: to the close, its line not said.
void DockRepair::abort() {
  sound = reg->sounds[9];
  hk().marker_clear();
  cancel(&reg->close);
}

// Mech3.exe 1.2 @ 0x0045b460: the drives in; the player's view back from the
// dock's camera.
void DockClose::begin() {
  FieldBaseHooks& h = hk();
  finished = false;
  queue_drives(*reg, drives, false);
  if (reg->requester == h.view_vehicle() && h.view_camera() == 4) h.view_set_camera(reg->camera);
}
bool DockClose::step(float) { return drives_done(drives); }  // 0x0045b620
// Mech3.exe 1.2 @ 0x0045b660
void DockClose::end() {
  free_drives(drives);
  field_bases_contact() = false;
}
Handle DockClose::message(Handle m) {  // 0x0045b770
  if (reg->requester != m) hk().say(reg->sounds[3]);
  return reg->requester;
}

// Mech3.exe 1.2 @ 0x0045b6c0: the bases held again, "mcbdeploy" 0. FIX
// (bug), ours: after a completed repair they stay called (DockParked).
void DockReset::begin() {
  finished = false;
  field_bases_contact() = false;
  if (reg->park) return;
  field_bases_drive() = false;
  hk().valve_set(reg->deploy_valve, 0);
}
// Mech3.exe 1.2 @ 0x0045b6f0
void DockReset::end() {
  const bool park = reg->park;
  reg->park = false;
  reg->states.push_back(park ? static_cast<DockStep*>(&reg->parked) : &reg->idle);
}

namespace {
// The player powered down within the dock point's 12 m.
bool player_docked(const FieldBases& r) {
  FieldBaseHooks& h = hk();
  const Handle me = h.player_vehicle();
  const float* at = r.dock_point ? h.dock_point_position(r.dock_point) : nullptr;
  const float* p = me ? h.entity_position(me) : nullptr;
  if (!at || !p) return false;
  const float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
  return dz * dz + dy * dy + dx * dx < kNear2 && h.powered_down(me);
}
}  // namespace

// Ours (FIX (bug), behind bugs; mech_fieldbase.h): the dock parked after a
// repair, idle to the MFB command, no mech asking. A player still down on
// the dock (just repaired) must power up or leave before asking again.
void DockParked::begin() {
  finished = false;
  armed = !player_docked(*reg);
  reg->requester = nullptr;
  reg->idle_on = true;
}
// A lancemate's refit sets the requester (FieldBases::set_requester: the bases
// may drive, not busy); the player asks by powering down within the dock's
// 12 m.
bool DockParked::step(float) {
  if (reg->requester) return true;
  if (!player_docked(*reg)) {
    armed = true;
    return false;
  }
  if (!armed) return false;
  reg->requester = hk().player_vehicle();
  return true;
}
// The next mech's turn: no places (the bases are on them).
void DockParked::end() {
  reg->idle_on = false;
  for (DockStep* st : {static_cast<DockStep*>(&reg->contact), static_cast<DockStep*>(&reg->deploy),
                       static_cast<DockStep*>(&reg->repair), static_cast<DockStep*>(&reg->close),
                       static_cast<DockStep*>(&reg->reset)})
    reg->states.push_back(st);
}
// M-M while parked: at this dock point, the caller is the next mech (the
// bases are on their places); at another, a new call as from the idle state,
// the parked state dropped unended.
Handle DockParked::message(Handle m) {
  if (hk().dock_point() == reg->dock_point) return reg->requester = m;
  std::list<DockStep*>& l = reg->states;
  l.clear();
  l.push_back(&reg->idle);
  reg->idle.begin();
  return reg->idle.message(m);
}

}  // namespace mech3
